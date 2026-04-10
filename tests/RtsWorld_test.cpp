#include <cmath>
#include <iostream>

#include "BuildingSystem.h"
#include "RtsPathfinder.h"
#include "TerrainGrid.h"
#include "RtsWorld.h"

namespace {
bool nearly_equal(float a, float b, float epsilon = 0.0001f) {
    return std::fabs(a - b) <= epsilon;
}

bool nearly_equal_vec3(const glm::vec3& a, const glm::vec3& b, float epsilon = 0.0001f) {
    return nearly_equal(a.x, b.x, epsilon) &&
           nearly_equal(a.y, b.y, epsilon) &&
           nearly_equal(a.z, b.z, epsilon);
}

float planar_distance(const glm::vec3& a, const glm::vec3& b) {
    return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.z - b.z) * (a.z - b.z));
}

void advance_world(RtsWorld& world, int steps, float delta_seconds) {
    for (int i = 0; i < steps; ++i) {
        world.update(delta_seconds);
    }
}

bool has_event_type(const std::vector<RtsCombatEvent>& events, RtsCombatEventType type) {
    for (const RtsCombatEvent& event : events) {
        if (event.type == type) {
            return true;
        }
    }
    return false;
}

bool has_unit_with_archetype(const RtsWorld& world, int team, const std::string& archetype_id) {
    for (const RtsWorldUnitSnapshot& unit : world.unitSnapshots()) {
        if (unit.team == team && unit.archetype_id == archetype_id) {
            return true;
        }
    }
    return false;
}

bool test_terrain_grid_queries() {
    TerrainGrid terrain(6, 5, 2.0f, glm::vec2(-6.0f, -4.0f));
    terrain.setTerrainType(GridCoord{2, 1}, TerrainType::forest);
    terrain.setTerrainType(GridCoord{3, 1}, TerrainType::road);
    terrain.setTerrainType(GridCoord{2, 2}, TerrainType::water);
    terrain.setElevation(GridCoord{3, 1}, 0.5f);

    GridCoord world_cell{};
    if (!terrain.worldToCell(glm::vec3(-0.2f, 0.0f, -1.1f), world_cell)) {
        return false;
    }
    if (world_cell.x != 2 || world_cell.y != 1) {
        return false;
    }

    const glm::vec3 center = terrain.cellCenter(GridCoord{3, 1});
    if (!nearly_equal(center.x, 1.0f) ||
        !nearly_equal(center.y, 0.5f) ||
        !nearly_equal(center.z, -1.0f)) {
        return false;
    }

    if (!terrain.isWalkable(GridCoord{2, 1}) ||
        terrain.isBuildable(GridCoord{2, 1}) ||
        !nearly_equal(terrain.movementCost(GridCoord{2, 1}), 1.9f)) {
        return false;
    }

    if (terrain.isWalkable(GridCoord{2, 2}) ||
        terrain.isBuildable(GridCoord{2, 2}) ||
        terrain.movementCost(GridCoord{2, 2}) < 1000.0f) {
        return false;
    }

    const float average_cost = terrain.averageMovementCost(GridCoord{2, 1}, 2, 1);
    return nearly_equal(average_cost, 1.3f, 0.001f);
}

bool test_building_placement_rules() {
    TerrainGrid terrain(8, 8, 1.0f);
    terrain.setTerrainType(GridCoord{5, 5}, TerrainType::water);
    terrain.setTerrainType(GridCoord{5, 6}, TerrainType::water);

    BuildingSystem buildings(terrain.width(), terrain.height());
    const BuildingDefinition town_hall{2, 3, true, true};
    const BuildingDefinition farm{2, 2, false, true};

    if (!buildings.canPlaceBuilding(terrain, town_hall, GridCoord{1, 2})) {
        return false;
    }

    const std::optional<std::uint32_t> town_hall_id =
        buildings.placeBuilding(terrain, town_hall, GridCoord{1, 2});
    if (!town_hall_id.has_value()) {
        return false;
    }

    if (buildings.buildingCount() != 1 ||
        !buildings.isCellOccupied(GridCoord{1, 2}) ||
        !buildings.isCellOccupied(GridCoord{2, 4}) ||
        buildings.buildingIdAtCell(GridCoord{2, 3}) != town_hall_id.value() ||
        buildings.findBuildingAtCell(GridCoord{2, 3}) == nullptr ||
        !buildings.blocksMovement(GridCoord{1, 3})) {
        return false;
    }

    if (buildings.canPlaceBuilding(terrain, farm, GridCoord{2, 3})) {
        return false;
    }
    if (buildings.canPlaceBuilding(terrain, farm, GridCoord{5, 5})) {
        return false;
    }

    const std::optional<std::uint32_t> farm_id =
        buildings.placeBuilding(terrain, farm, GridCoord{4, 1});
    if (!farm_id.has_value() ||
        !buildings.isCellOccupied(GridCoord{5, 2}) ||
        buildings.blocksMovement(GridCoord{5, 2})) {
        return false;
    }

    if (!buildings.removeBuilding(town_hall_id.value())) {
        return false;
    }
    if (buildings.isCellOccupied(GridCoord{1, 2}) ||
        buildings.blocksMovement(GridCoord{1, 2}) ||
        buildings.buildingCount() != 1) {
        return false;
    }

    return buildings.findBuilding(farm_id.value()) != nullptr;
}

bool test_pathfinder_routes_around_blockers() {
    TerrainGrid terrain(7, 7, 1.0f);
    for (int y = 0; y < terrain.height(); ++y) {
        if (y == 3) {
            continue;
        }
        terrain.setTerrainType(GridCoord{3, y}, TerrainType::water);
    }

    BuildingSystem buildings(terrain.width(), terrain.height());
    RtsPathfinder pathfinder{};
    const std::vector<GridCoord> path =
        pathfinder.findPath(terrain, buildings, GridCoord{1, 1}, GridCoord{5, 1});

    if (path.empty() || path.front().x != 1 || path.front().y != 1 ||
        path.back().x != 5 || path.back().y != 1) {
        return false;
    }

    bool passes_gap = false;
    for (const GridCoord& cell : path) {
        if (cell.x == 3 && cell.y == 3) {
            passes_gap = true;
        }
        if (!terrain.isWalkable(cell)) {
            return false;
        }
    }
    return passes_gap;
}

bool test_pathfinder_prefers_lower_cost_terrain() {
    TerrainGrid terrain(8, 5, 1.0f);
    for (int x = 1; x <= 6; ++x) {
        terrain.setTerrainType(GridCoord{x, 2}, TerrainType::forest);
    }
    for (int x = 0; x <= 7; ++x) {
        terrain.setTerrainType(GridCoord{x, 1}, TerrainType::road);
    }

    BuildingSystem buildings(terrain.width(), terrain.height());
    RtsPathfinder pathfinder{};
    const std::vector<GridCoord> path =
        pathfinder.findPath(terrain, buildings, GridCoord{0, 2}, GridCoord{7, 2});
    if (path.empty()) {
        return false;
    }

    bool uses_road = false;
    for (const GridCoord& cell : path) {
        if (cell.y == 1) {
            uses_road = true;
        }
        if (buildings.blocksMovement(cell)) {
            return false;
        }
    }
    return uses_road;
}

bool test_rts_world_move_queue() {
    RtsWorld world(12, 12, 1.0f);
    if (!world.addUnit(100, 0, glm::vec3(1.5f, 0.0f, 1.5f), 2.0f)) {
        return false;
    }

    const RtsOrder first_leg{
        RtsOrderType::move,
        glm::vec3(5.5f, 0.0f, 1.5f),
        glm::vec3(0.0f),
        0,
        2.0f,
        0.1f
    };
    const RtsOrder second_leg{
        RtsOrderType::move,
        glm::vec3(5.5f, 0.0f, 5.5f),
        glm::vec3(0.0f),
        0,
        2.0f,
        0.1f
    };

    if (!world.issueOrder(100, first_leg) ||
        !world.issueOrder(100, second_leg, true) ||
        world.queuedOrderCount(100) != 1) {
        return false;
    }

    advance_world(world, 60, 0.1f);
    return nearly_equal_vec3(world.getUnitPosition(100), glm::vec3(5.5f, 0.0f, 5.5f), 0.15f) &&
           !world.activeOrderType(100).has_value() &&
           world.queuedOrderCount(100) == 0 &&
           !world.isUnitMoving(100);
}

bool test_rts_world_hold_and_stop_orders() {
    RtsWorld world(12, 12, 1.0f);
    if (!world.addUnit(110, 0, glm::vec3(1.5f, 0.0f, 1.5f), 2.0f)) {
        return false;
    }

    const RtsOrder move_order{
        RtsOrderType::move,
        glm::vec3(8.5f, 0.0f, 1.5f),
        glm::vec3(0.0f),
        0,
        2.0f,
        0.1f
    };
    if (!world.issueOrder(110, move_order)) {
        return false;
    }

    advance_world(world, 5, 0.1f);
    if (!world.issueOrder(110, RtsOrder{
            RtsOrderType::hold_position,
            glm::vec3(0.0f),
            glm::vec3(0.0f),
            0,
            0.0f,
            0.0f
        })) {
        return false;
    }

    const glm::vec3 held_position = world.getUnitPosition(110);
    advance_world(world, 10, 0.1f);
    if (!world.isHoldingPosition(110) ||
        !nearly_equal_vec3(held_position, world.getUnitPosition(110), 0.001f)) {
        return false;
    }

    const RtsOrder queued_move{
        RtsOrderType::move,
        glm::vec3(6.5f, 0.0f, 6.5f),
        glm::vec3(0.0f),
        0,
        2.0f,
        0.1f
    };
    const RtsOrder queued_move_two{
        RtsOrderType::move,
        glm::vec3(9.5f, 0.0f, 6.5f),
        glm::vec3(0.0f),
        0,
        2.0f,
        0.1f
    };
    if (!world.issueOrder(110, queued_move) ||
        !world.issueOrder(110, queued_move_two, true)) {
        return false;
    }

    advance_world(world, 5, 0.1f);
    if (!world.issueOrder(110, RtsOrder{
            RtsOrderType::stop,
            glm::vec3(0.0f),
            glm::vec3(0.0f),
            0,
            0.0f,
            0.0f
        })) {
        return false;
    }

    const glm::vec3 stopped_position = world.getUnitPosition(110);
    advance_world(world, 10, 0.1f);
    return !world.isHoldingPosition(110) &&
           !world.isUnitMoving(110) &&
           !world.activeOrderType(110).has_value() &&
           world.queuedOrderCount(110) == 0 &&
           nearly_equal_vec3(stopped_position, world.getUnitPosition(110), 0.001f);
}

bool test_rts_world_patrol_order() {
    RtsWorld world(12, 12, 1.0f);
    if (!world.addUnit(120, 0, glm::vec3(2.5f, 0.0f, 2.5f), 2.0f)) {
        return false;
    }

    if (!world.issueOrder(120, RtsOrder{
            RtsOrderType::patrol,
            glm::vec3(6.5f, 0.0f, 2.5f),
            glm::vec3(2.5f, 0.0f, 2.5f),
            0,
            2.0f,
            0.1f
        })) {
        return false;
    }

    advance_world(world, 21, 0.1f);
    const bool reached_far_point =
        nearly_equal_vec3(world.getUnitPosition(120), glm::vec3(6.5f, 0.0f, 2.5f), 0.15f);
    advance_world(world, 21, 0.1f);
    const bool returned_home =
        nearly_equal_vec3(world.getUnitPosition(120), glm::vec3(2.5f, 0.0f, 2.5f), 0.15f);
    return reached_far_point &&
           returned_home &&
           world.activeOrderType(120).has_value() &&
           world.activeOrderType(120).value() == RtsOrderType::patrol;
}

bool test_rts_world_guard_order() {
    RtsWorld world(12, 12, 1.0f);
    if (!world.addUnit(130, 0, glm::vec3(1.5f, 0.0f, 1.5f), 2.0f) ||
        !world.addUnit(131, 0, glm::vec3(1.5f, 0.0f, 4.5f), 2.5f)) {
        return false;
    }

    if (!world.issueOrder(131, RtsOrder{
            RtsOrderType::guard,
            glm::vec3(0.0f),
            glm::vec3(0.0f),
            130,
            2.5f,
            0.1f
        }) ||
        !world.issueOrder(130, RtsOrder{
            RtsOrderType::move,
            glm::vec3(8.5f, 0.0f, 1.5f),
            glm::vec3(0.0f),
            0,
            2.0f,
            0.1f
        })) {
        return false;
    }

    advance_world(world, 45, 0.1f);
    const glm::vec3 guarded = world.getUnitPosition(130);
    const glm::vec3 guard = world.getUnitPosition(131);
    return planar_distance(guarded, guard) <= 1.6f &&
           world.activeOrderType(131).has_value() &&
           world.activeOrderType(131).value() == RtsOrderType::guard;
}

bool test_rts_world_attack_move_order() {
    RtsWorld world(12, 12, 1.0f);
    if (!world.addUnit(140, 0, glm::vec3(1.5f, 0.0f, 1.5f), 2.0f) ||
        !world.addUnit(141, 1, glm::vec3(3.5f, 0.0f, 1.5f), 0.5f)) {
        return false;
    }

    if (!world.issueOrder(140, RtsOrder{
            RtsOrderType::attack_move,
            glm::vec3(9.5f, 0.0f, 1.5f),
            glm::vec3(0.0f),
            0,
            2.0f,
            0.1f
        })) {
        return false;
    }

    advance_world(world, 8, 0.1f);
    if (world.projectileSnapshots().empty() && nearly_equal(world.unitHealth(141), 100.0f)) {
        return false;
    }

    advance_world(world, 100, 0.1f);
    return !world.hasUnit(141) &&
           nearly_equal_vec3(world.getUnitPosition(140), glm::vec3(9.5f, 0.0f, 1.5f), 0.25f) &&
           !world.activeOrderType(140).has_value() &&
           world.winningTeam().has_value() &&
           world.winningTeam().value() == 0;
}

bool test_rts_world_hold_position_attacks_without_moving() {
    RtsWorld world(12, 12, 1.0f);
    if (!world.addUnit(150, 0, glm::vec3(2.5f, 0.0f, 2.5f), 2.0f, 0.35f, 3.5f, 1.5f, 2.1f) ||
        !world.addUnit(151, 1, glm::vec3(4.0f, 0.0f, 2.5f), 1.0f)) {
        return false;
    }

    if (!world.issueOrder(150, RtsOrder{
            RtsOrderType::hold_position,
            glm::vec3(0.0f),
            glm::vec3(0.0f),
            0,
            0.0f,
            0.0f
        })) {
        return false;
    }

    const glm::vec3 held_position = world.getUnitPosition(150);
    advance_world(world, 20, 0.1f);
    return world.isHoldingPosition(150) &&
           nearly_equal_vec3(held_position, world.getUnitPosition(150), 0.001f) &&
           (!world.projectileSnapshots().empty() || world.unitHealth(151) < 100.0f);
}

bool test_rts_world_tower_auto_attack_and_win_condition() {
    RtsWorld world(12, 12, 1.0f);
    const BuildingDefinition tower_definition{2, 2, true, true};
    const std::optional<std::uint32_t> building_id =
        world.buildings().placeBuilding(world.terrain(), tower_definition, GridCoord{4, 4});
    if (!building_id.has_value() || !world.addTower(900, building_id.value(), 0, 4.5f, 25.0f, 0.8f, 6.0f)) {
        return false;
    }
    if (!world.addUnit(170, 1, glm::vec3(7.5f, 0.0f, 5.0f), 1.0f)) {
        return false;
    }

    advance_world(world, 6, 0.1f);
    if (world.projectileSnapshots().empty() && nearly_equal(world.unitHealth(170), 100.0f)) {
        return false;
    }

    advance_world(world, 60, 0.1f);
    return world.hasTower(900) &&
           !world.hasUnit(170) &&
           world.isMatchOver() &&
           world.winningTeam().has_value() &&
           world.winningTeam().value() == 0;
}

bool test_rts_world_archetypes_and_building_snapshots() {
    RtsWorld world(12, 12, 1.0f);

    if (!world.registerUnitArchetype("soldier", RtsUnitArchetype{
            2.4f, 0.35f, 4.0f, 1.5f, 1.8f, 125.0f, 19.0f, 0.65f, 8.0f
        })) {
        return false;
    }
    if (!world.registerBuildingArchetype("tower", RtsBuildingArchetype{
            BuildingDefinition{2, 2, true, true},
            true,
            true,
            4.5f,
            13.0f,
            0.9f,
            7.5f
        })) {
        return false;
    }
    if (world.findUnitArchetype("soldier") == nullptr ||
        world.findBuildingArchetype("tower") == nullptr) {
        return false;
    }
    if (!world.addUnitFromArchetype(200, 0, glm::vec3(2.5f, 0.0f, 2.5f), "soldier")) {
        return false;
    }

    const auto unit_snapshot = world.getUnitSnapshot(200);
    if (!unit_snapshot.has_value() ||
        !nearly_equal(unit_snapshot->max_health, 125.0f) ||
        !nearly_equal(unit_snapshot->health, 125.0f) ||
        !nearly_equal(unit_snapshot->radius, 0.35f)) {
        return false;
    }

    if (!world.canPlaceBuildingFromArchetype("tower", GridCoord{5, 5})) {
        return false;
    }
    const std::optional<std::uint32_t> building_id =
        world.placeBuildingFromArchetype(1, "tower", GridCoord{5, 5});
    if (!building_id.has_value()) {
        return false;
    }

    const std::vector<RtsWorldBuildingSnapshot> buildings = world.buildingSnapshots();
    if (buildings.size() != 1 ||
        buildings.front().building_id != building_id.value() ||
        buildings.front().team != 1 ||
        buildings.front().archetype_id != "tower" ||
        !buildings.front().registers_tower ||
        !buildings.front().blocks_movement ||
        world.towerCount() != 1 ||
        !world.hasTower(building_id.value()) ||
        !has_event_type(world.combatEvents(), RtsCombatEventType::building_placed)) {
        return false;
    }

    if (world.canPlaceBuildingFromArchetype("tower", GridCoord{5, 5})) {
        return false;
    }
    if (!world.removeBuilding(building_id.value()) ||
        world.hasTower(building_id.value()) ||
        !has_event_type(world.combatEvents(), RtsCombatEventType::building_removed)) {
        return false;
    }
    return world.buildingSnapshots().empty();
}

bool test_rts_world_formation_order_api() {
    RtsWorld world(16, 16, 1.0f);
    const std::vector<std::uint32_t> unit_ids{300, 301, 302, 303};
    for (std::size_t i = 0; i < unit_ids.size(); ++i) {
        if (!world.addUnit(unit_ids[i],
                           0,
                           glm::vec3(2.5f + static_cast<float>(i) * 0.4f, 0.0f, 2.5f),
                           2.6f)) {
            return false;
        }
    }

    if (!world.issueFormationOrder(unit_ids,
                                   glm::vec3(9.5f, 0.0f, 9.5f),
                                   RtsOrderType::move,
                                   1.4f)) {
        return false;
    }

    advance_world(world, 80, 0.1f);
    float min_x = std::numeric_limits<float>::max();
    float max_x = std::numeric_limits<float>::lowest();
    float min_z = std::numeric_limits<float>::max();
    float max_z = std::numeric_limits<float>::lowest();
    glm::vec3 centroid(0.0f);
    for (const std::uint32_t unit_id : unit_ids) {
        const glm::vec3 position = world.getUnitPosition(unit_id);
        centroid += position;
        min_x = std::min(min_x, position.x);
        max_x = std::max(max_x, position.x);
        min_z = std::min(min_z, position.z);
        max_z = std::max(max_z, position.z);
        if (world.activeOrderType(unit_id).has_value()) {
            return false;
        }
    }
    centroid /= static_cast<float>(unit_ids.size());

    return planar_distance(centroid, glm::vec3(9.5f, 0.0f, 9.5f)) <= 0.5f &&
           (max_x - min_x) >= 1.0f &&
           (max_z - min_z) >= 1.0f;
}

bool test_rts_world_combat_event_output() {
    RtsWorld world(12, 12, 1.0f);
    if (!world.registerUnitArchetype("skirmisher", RtsUnitArchetype{
            2.5f, 0.35f, 4.0f, 1.5f, 2.0f, 60.0f, 30.0f, 0.5f, 9.0f
        })) {
        return false;
    }
    if (!world.addUnitFromArchetype(400, 0, glm::vec3(2.5f, 0.0f, 2.5f), "skirmisher") ||
        !world.addUnitFromArchetype(401, 1, glm::vec3(4.0f, 0.0f, 2.5f), "skirmisher")) {
        return false;
    }
    if (!world.issueOrder(400, RtsOrder{
            RtsOrderType::attack_move,
            glm::vec3(8.5f, 0.0f, 2.5f),
            glm::vec3(0.0f),
            401,
            2.5f,
            0.1f
        })) {
        return false;
    }

    bool saw_projectile_spawned = false;
    bool saw_projectile_hit = false;
    bool saw_unit_died = false;
    bool saw_match_ended = false;
    for (int step = 0; step < 80; ++step) {
        world.update(0.1f);
        const std::vector<RtsCombatEvent>& events = world.combatEvents();
        saw_projectile_spawned |= has_event_type(events, RtsCombatEventType::projectile_spawned);
        saw_projectile_hit |= has_event_type(events, RtsCombatEventType::projectile_hit);
        saw_unit_died |= has_event_type(events, RtsCombatEventType::unit_died);
        saw_match_ended |= has_event_type(events, RtsCombatEventType::match_ended);
        if (!world.hasUnit(401) && saw_match_ended) {
            break;
        }
    }

    return saw_projectile_spawned &&
           saw_projectile_hit &&
           saw_unit_died &&
           saw_match_ended &&
           !world.hasUnit(401) &&
           world.winningTeam().has_value() &&
           world.winningTeam().value() == 0;
}

bool test_rts_world_resource_economy_api() {
    RtsWorld world(12, 12, 1.0f);
    world.setTeamResourceAmount(0, "ore", 120);
    if (world.teamResourceAmount(0, "ore") != 120) {
        return false;
    }
    if (world.addTeamResourceAmount(0, "ore", -35) != 85 ||
        world.addTeamResourceAmount(0, "gas", 20) != 20) {
        return false;
    }

    const std::vector<RtsTeamResourceSnapshot> resources = world.teamResourceSnapshots(0);
    if (resources.size() != 2 ||
        resources[0].resource_id != "gas" ||
        resources[0].amount != 20 ||
        resources[1].resource_id != "ore" ||
        resources[1].amount != 85) {
        return false;
    }

    const std::optional<std::uint32_t> node_id = world.addResourceNode("ore", GridCoord{4, 4}, 75);
    if (!node_id.has_value()) {
        return false;
    }

    const std::vector<RtsWorldResourceNodeSnapshot> nodes = world.resourceNodeSnapshots();
    if (nodes.size() != 1 ||
        nodes.front().node_id != node_id.value() ||
        nodes.front().resource_id != "ore" ||
        nodes.front().remaining_amount != 75) {
        return false;
    }

    if (world.harvestResourceNode(node_id.value(), 30) != 30 ||
        world.resourceNodeAmount(node_id.value()) != 45 ||
        world.harvestResourceNode(node_id.value(), 99) != 45 ||
        world.resourceNodeAmount(node_id.value()) != 0 ||
        !world.resourceNodeSnapshots().empty()) {
        return false;
    }

    return world.canAffordCosts(0, std::vector<RtsResourceCost>{{"ore", 50}, {"gas", 10}}) &&
           !world.canAffordCosts(0, std::vector<RtsResourceCost>{{"ore", 90}});
}

bool test_rts_world_production_api() {
    RtsWorld world(16, 16, 1.0f);

    if (!world.registerUnitArchetype("worker", RtsUnitArchetype{
            2.2f,
            0.35f,
            3.5f,
            1.5f,
            1.2f,
            60.0f,
            6.0f,
            0.8f,
            7.5f,
            {RtsResourceCost{"ore", 50}},
            0.6f,
            1
        })) {
        return false;
    }
    if (!world.registerBuildingArchetype("barracks", RtsBuildingArchetype{
            BuildingDefinition{3, 2, true, true},
            true,
            false,
            0.0f,
            0.0f,
            0.0f,
            0.0f,
            {},
            {"worker"},
            2,
            false
        })) {
        return false;
    }

    world.setTeamResourceAmount(0, "ore", 150);
    const std::optional<std::uint32_t> building_id =
        world.placeBuildingFromArchetype(0, "barracks", GridCoord{5, 5});
    if (!building_id.has_value()) {
        return false;
    }

    if (!world.setProductionRallyPoint(building_id.value(), glm::vec3(12.5f, 0.0f, 12.5f)) ||
        !world.canProduceUnitFromBuilding(building_id.value(), "worker") ||
        !world.enqueueProduction(building_id.value(), "worker") ||
        !world.enqueueProduction(building_id.value(), "worker") ||
        world.enqueueProduction(building_id.value(), "worker") ||
        world.teamResourceAmount(0, "ore") != 50) {
        return false;
    }

    const std::vector<RtsWorldProductionSnapshot> production_before_cancel = world.productionSnapshots();
    if (production_before_cancel.size() != 1 ||
        production_before_cancel.front().queue.size() != 2 ||
        !production_before_cancel.front().queue.front().active) {
        return false;
    }

    if (!world.cancelLastProduction(building_id.value()) ||
        world.teamResourceAmount(0, "ore") != 100 ||
        world.productionSnapshots().front().queue.size() != 1) {
        return false;
    }

    advance_world(world, 8, 0.1f);
    if (world.unitCount() != 1 ||
        world.teamSupplyUsed(0) != 1 ||
        world.teamSupplyProvided(0) != 2 ||
        !world.productionSnapshots().front().queue.empty()) {
        return false;
    }

    if (!world.enqueueProduction(building_id.value(), "worker") ||
        world.teamResourceAmount(0, "ore") != 50) {
        return false;
    }

    advance_world(world, 8, 0.1f);
    if (world.unitCount() != 2 ||
        world.teamSupplyUsed(0) != 2 ||
        !world.productionSnapshots().front().queue.empty()) {
        return false;
    }

    return !world.canProduceUnitFromBuilding(building_id.value(), "worker");
}

bool test_rts_world_worker_harvest_and_deposit() {
    RtsWorld world(16, 16, 1.0f);
    if (!world.registerUnitArchetype("worker", RtsUnitArchetype{
            2.2f,
            0.35f,
            3.0f,
            1.5f,
            1.0f,
            50.0f,
            4.0f,
            1.0f,
            7.0f,
            {},
            0.0f,
            1,
            true,
            10,
            5,
            0.2f
        })) {
        return false;
    }
    if (!world.registerBuildingArchetype("depot", RtsBuildingArchetype{
            BuildingDefinition{2, 2, true, true},
            true,
            false,
            0.0f,
            0.0f,
            0.0f,
            0.0f,
            {},
            {},
            5,
            true
        })) {
        return false;
    }

    const std::optional<std::uint32_t> depot_id =
        world.placeBuildingFromArchetype(0, "depot", GridCoord{2, 2});
    const std::optional<std::uint32_t> node_id =
        world.addResourceNode("ore", GridCoord{8, 8}, 25);
    if (!depot_id.has_value() ||
        !node_id.has_value() ||
        !world.addUnitFromArchetype(500, 0, glm::vec3(5.5f, 0.0f, 5.5f), "worker") ||
        !world.issueHarvestOrder(500, node_id.value())) {
        return false;
    }

    bool saw_harvest_event = false;
    bool saw_deposit_event = false;
    for (int step = 0; step < 120; ++step) {
        world.update(0.1f);
        saw_harvest_event |= has_event_type(world.events(), RtsEventType::resource_harvested);
        saw_deposit_event |= has_event_type(world.events(), RtsEventType::resources_deposited);
        if (world.teamResourceAmount(0, "ore") >= 10) {
            break;
        }
    }

    const auto worker_snapshot = world.getUnitSnapshot(500);
    return worker_snapshot.has_value() &&
           saw_harvest_event &&
           saw_deposit_event &&
           world.teamResourceAmount(0, "ore") >= 10 &&
           worker_snapshot->carried_resource_amount >= 0;
}

bool test_rts_world_enemy_ai_harvests_produces_and_attacks() {
    RtsWorld world(20, 20, 1.0f);
    if (!world.registerUnitArchetype("worker", RtsUnitArchetype{
            2.25f,
            0.35f,
            3.0f,
            1.5f,
            1.0f,
            55.0f,
            4.0f,
            1.0f,
            7.0f,
            {RtsResourceCost{"ore", 40}},
            0.6f,
            1,
            true,
            10,
            5,
            0.2f
        }) ||
        !world.registerUnitArchetype("soldier", RtsUnitArchetype{
            2.5f,
            0.35f,
            4.0f,
            1.5f,
            1.9f,
            80.0f,
            15.0f,
            0.6f,
            8.5f,
            {RtsResourceCost{"ore", 40}},
            0.5f,
            1
        }) ||
        !world.registerBuildingArchetype("depot", RtsBuildingArchetype{
            BuildingDefinition{3, 2, true, true},
            true,
            false,
            0.0f,
            0.0f,
            0.0f,
            0.0f,
            {},
            {"worker", "soldier"},
            6,
            true
        })) {
        return false;
    }

    const std::optional<std::uint32_t> player_depot =
        world.placeBuildingFromArchetype(0, "depot", GridCoord{2, 2});
    const std::optional<std::uint32_t> enemy_depot =
        world.placeBuildingFromArchetype(1, "depot", GridCoord{14, 14});
    const std::optional<std::uint32_t> ore_node =
        world.addResourceNode("ore", GridCoord{12, 14}, 120);
    if (!player_depot.has_value() ||
        !enemy_depot.has_value() ||
        !ore_node.has_value() ||
        !world.addUnitFromArchetype(100, 0, glm::vec3(4.5f, 0.0f, 4.5f), "soldier") ||
        !world.addUnitFromArchetype(500, 1, glm::vec3(14.5f, 0.0f, 13.5f), "worker")) {
        return false;
    }

    world.setAiProfile(1, RtsAiProfile{
        0.2f,
        1,
        1,
        8.0f,
        true,
        true,
        true,
        "worker",
        {"soldier"}
    });

    bool saw_harvest_event = false;
    bool saw_deposit_event = false;
    bool saw_production_started = false;
    bool saw_unit_spawned = false;
    bool saw_attack_move = false;
    bool saw_forward_progress = false;
    for (int step = 0; step < 300; ++step) {
        world.update(0.1f);
        saw_harvest_event |= has_event_type(world.events(), RtsEventType::resource_harvested);
        saw_deposit_event |= has_event_type(world.events(), RtsEventType::resources_deposited);
        saw_production_started |= has_event_type(world.events(), RtsEventType::production_started);
        saw_unit_spawned |= has_event_type(world.events(), RtsEventType::unit_spawned);
        if (world.hasUnit(501)) {
            const auto produced = world.getUnitSnapshot(501);
            saw_attack_move |= world.activeOrderType(501).has_value() &&
                               world.activeOrderType(501).value() == RtsOrderType::attack_move;
            saw_forward_progress |= produced.has_value() && produced->position.x < 12.0f;
        }
        if (!world.hasUnit(100) || saw_forward_progress) {
            break;
        }
    }

    return saw_harvest_event &&
           saw_deposit_event &&
           saw_production_started &&
           saw_unit_spawned &&
           has_unit_with_archetype(world, 1, "soldier") &&
           (saw_attack_move || saw_forward_progress || !world.hasUnit(100));
}

bool test_rts_world_enemy_ai_scouts_when_under_attack_threshold() {
    RtsWorld world(20, 20, 1.0f);
    if (!world.registerUnitArchetype("soldier", RtsUnitArchetype{
            2.6f, 0.35f, 4.0f, 1.5f, 1.9f, 80.0f, 15.0f, 0.6f, 8.5f, {}, 0.0f, 1
        }) ||
        !world.registerBuildingArchetype("depot", RtsBuildingArchetype{
            BuildingDefinition{3, 2, true, true},
            true,
            false,
            0.0f,
            0.0f,
            0.0f,
            0.0f,
            {},
            {},
            6,
            true
        })) {
        return false;
    }

    if (!world.placeBuildingFromArchetype(0, "depot", GridCoord{2, 2}).has_value() ||
        !world.placeBuildingFromArchetype(1, "depot", GridCoord{14, 14}).has_value() ||
        !world.addUnitFromArchetype(500, 1, glm::vec3(14.5f, 0.0f, 13.5f), "soldier")) {
        return false;
    }

    world.setAiProfile(1, RtsAiProfile{
        0.2f,
        0,
        3,
        6.0f,
        false,
        false,
        true,
        "worker",
        {"soldier"}
    });

    bool moved_forward = false;
    for (int step = 0; step < 80; ++step) {
        world.update(0.1f);
        const auto scout = world.getUnitSnapshot(500);
        if (!scout.has_value()) {
            return false;
        }
        moved_forward |= scout->position.x < 13.0f || scout->position.z < 13.0f;
        if (moved_forward) {
            return true;
        }
    }
    return false;
}

bool test_rts_world_enemy_ai_prioritizes_combat_targets() {
    RtsWorld world(20, 20, 1.0f);
    if (!world.registerUnitArchetype("worker", RtsUnitArchetype{
            2.2f, 0.35f, 2.8f, 1.5f, 1.0f, 55.0f, 3.0f, 1.0f, 7.0f, {}, 0.0f, 1, true, 10, 5, 0.2f
        }) ||
        !world.registerUnitArchetype("soldier", RtsUnitArchetype{
            2.5f, 0.35f, 4.0f, 1.5f, 1.9f, 80.0f, 15.0f, 0.6f, 8.5f, {}, 0.0f, 1
        }) ||
        !world.registerBuildingArchetype("depot", RtsBuildingArchetype{
            BuildingDefinition{3, 2, true, true},
            true,
            false,
            0.0f,
            0.0f,
            0.0f,
            0.0f,
            {},
            {},
            6,
            true
        })) {
        return false;
    }

    if (!world.placeBuildingFromArchetype(1, "depot", GridCoord{11, 11}).has_value() ||
        !world.addUnitFromArchetype(500, 1, glm::vec3(11.5f, 0.0f, 10.5f), "soldier") ||
        !world.addUnitFromArchetype(501, 1, glm::vec3(12.0f, 0.0f, 10.8f), "soldier") ||
        !world.addUnitFromArchetype(100, 0, glm::vec3(9.2f, 0.0f, 10.8f), "worker") ||
        !world.addUnitFromArchetype(101, 0, glm::vec3(9.6f, 0.0f, 9.4f), "soldier")) {
        return false;
    }

    world.setAiProfile(1, RtsAiProfile{
        0.2f,
        0,
        1,
        8.0f,
        false,
        false,
        true,
        "worker",
        {"soldier"}
    });

    for (int step = 0; step < 80; ++step) {
        world.update(0.1f);
        if (!world.hasUnit(101)) {
            break;
        }
    }

    return !world.hasUnit(101) && world.hasUnit(100);
}

bool test_rts_world_enemy_ai_retreats_wounded_units() {
    RtsWorld world(20, 20, 1.0f);
    if (!world.registerUnitArchetype("soldier", RtsUnitArchetype{
            2.5f, 0.35f, 4.0f, 1.5f, 1.9f, 80.0f, 14.0f, 0.5f, 8.5f, {}, 0.0f, 1
        }) ||
        !world.registerBuildingArchetype("depot", RtsBuildingArchetype{
            BuildingDefinition{3, 2, true, true},
            true,
            false,
            0.0f,
            0.0f,
            0.0f,
            0.0f,
            {},
            {},
            6,
            true
        })) {
        return false;
    }

    if (!world.placeBuildingFromArchetype(1, "depot", GridCoord{14, 14}).has_value() ||
        !world.addUnitFromArchetype(500, 1, glm::vec3(10.5f, 0.0f, 10.5f), "soldier") ||
        !world.addUnitFromArchetype(501, 1, glm::vec3(16.5f, 0.0f, 16.5f), "soldier") ||
        !world.addUnitFromArchetype(100, 0, glm::vec3(8.8f, 0.0f, 10.5f), "soldier")) {
        return false;
    }

    if (!world.issueOrder(100, RtsOrder{
            RtsOrderType::attack_move,
            glm::vec3(10.5f, 0.0f, 10.5f),
            glm::vec3(0.0f),
            500,
            2.5f,
            0.1f
        })) {
        return false;
    }

    for (int step = 0; step < 80; ++step) {
        world.update(0.1f);
        if (world.unitHealth(500) < 40.0f) {
            break;
        }
    }
    if (world.unitHealth(500) >= 40.0f || !world.hasUnit(500)) {
        return false;
    }

    const glm::vec3 before = world.getUnitPosition(500);
    world.setAiProfile(1, RtsAiProfile{
        0.2f,
        0,
        3,
        8.0f,
        false,
        false,
        true,
        "worker",
        {"soldier"}
    });

    world.update(0.1f);
    const std::optional<RtsOrderType> retreat_order = world.activeOrderType(500);
    advance_world(world, 8, 0.1f);
    const glm::vec3 after = world.getUnitPosition(500);
    return retreat_order.has_value() &&
           retreat_order.value() == RtsOrderType::move &&
           (after.x > before.x || after.z > before.z);
}

bool test_rts_world_enemy_ai_flanks_large_attack_waves() {
    RtsWorld world(24, 24, 1.0f);
    if (!world.registerUnitArchetype("soldier", RtsUnitArchetype{
            2.5f, 0.35f, 4.0f, 1.5f, 1.9f, 80.0f, 14.0f, 0.6f, 8.5f, {}, 0.0f, 1
        }) ||
        !world.registerBuildingArchetype("depot", RtsBuildingArchetype{
            BuildingDefinition{3, 2, true, true},
            true,
            false,
            0.0f,
            0.0f,
            0.0f,
            0.0f,
            {},
            {},
            6,
            true
        })) {
        return false;
    }

    if (!world.placeBuildingFromArchetype(0, "depot", GridCoord{2, 2}).has_value() ||
        !world.placeBuildingFromArchetype(1, "depot", GridCoord{18, 18}).has_value()) {
        return false;
    }
    for (std::uint32_t i = 0; i < 4; ++i) {
        if (!world.addUnitFromArchetype(500 + i,
                                        1,
                                        glm::vec3(18.2f + static_cast<float>(i % 2) * 0.5f,
                                                  0.0f,
                                                  16.8f + static_cast<float>(i / 2) * 0.5f),
                                        "soldier")) {
            return false;
        }
    }

    world.setAiProfile(1, RtsAiProfile{
        0.2f,
        0,
        2,
        6.0f,
        false,
        false,
        true,
        "worker",
        {"soldier"}
    });

    world.update(0.1f);
    int move_orders = 0;
    for (std::uint32_t i = 0; i < 4; ++i) {
        const auto order = world.activeOrderType(500 + i);
        if (order.has_value() && order.value() == RtsOrderType::move) {
            ++move_orders;
        }
    }
    advance_world(world, 8, 0.1f);
    const glm::vec3 left = world.getUnitPosition(500);
    const glm::vec3 right = world.getUnitPosition(501);
    return move_orders >= 4 && std::fabs(left.z - right.z) > 0.4f;
}

bool test_rts_world_building_construction_and_cancel() {
    RtsWorld world(16, 16, 1.0f);
    if (!world.registerUnitArchetype("worker", RtsUnitArchetype{
            2.3f, 0.35f, 3.0f, 1.5f, 1.0f, 55.0f, 4.0f, 1.0f, 7.0f, {}, 0.0f, 1, true, 10, 5, 0.2f
        }) ||
        !world.registerBuildingArchetype("outpost", RtsBuildingArchetype{
            BuildingDefinition{2, 2, true, true},
            true,
            false,
            0.0f,
            0.0f,
            0.0f,
            0.0f,
            {RtsResourceCost{"ore", 80}},
            {},
            2,
            false,
            180.0f,
            1.2f,
            35.0f
        })) {
        return false;
    }
    if (!world.addUnitFromArchetype(500, 0, glm::vec3(3.5f, 0.0f, 3.5f), "worker") ||
        !world.addUnitFromArchetype(501, 0, glm::vec3(4.5f, 0.0f, 3.5f), "worker")) {
        return false;
    }

    world.setTeamResourceAmount(0, "ore", 200);
    const std::optional<std::uint32_t> building_id =
        world.startBuildingConstruction(0, "outpost", GridCoord{6, 6}, 500, true);
    if (!building_id.has_value() || world.teamResourceAmount(0, "ore") != 120) {
        return false;
    }

    const auto initial_snapshot = world.getBuildingSnapshot(building_id.value());
    if (!initial_snapshot.has_value() ||
        !initial_snapshot->under_construction ||
        initial_snapshot->completed ||
        initial_snapshot->health <= 0.0f ||
        initial_snapshot->health >= initial_snapshot->max_health ||
        !has_event_type(world.events(), RtsEventType::construction_started)) {
        return false;
    }

    advance_world(world, 8, 0.1f);
    const auto in_progress_snapshot = world.getBuildingSnapshot(building_id.value());
    if (!in_progress_snapshot.has_value() ||
        in_progress_snapshot->construction_progress <= 0.0f ||
        in_progress_snapshot->construction_progress >= 1.0f) {
        return false;
    }

    const std::optional<std::uint32_t> canceled_id =
        world.startBuildingConstruction(0, "outpost", GridCoord{10, 6}, 501, true);
    if (!canceled_id.has_value() || world.teamResourceAmount(0, "ore") != 40) {
        return false;
    }
    if (!world.cancelBuildingConstruction(canceled_id.value(), true) ||
        world.teamResourceAmount(0, "ore") != 120 ||
        world.getBuildingSnapshot(canceled_id.value()).has_value() ||
        !has_event_type(world.events(), RtsEventType::construction_canceled)) {
        return false;
    }

    bool saw_completion_event = false;
    for (int step = 0; step < 24; ++step) {
        world.update(0.1f);
        saw_completion_event |= has_event_type(world.events(), RtsEventType::construction_completed);
        if (world.isBuildingComplete(building_id.value())) {
            break;
        }
    }
    const auto completed_snapshot = world.getBuildingSnapshot(building_id.value());
    return completed_snapshot.has_value() &&
           completed_snapshot->completed &&
           !completed_snapshot->under_construction &&
           nearly_equal(completed_snapshot->health, completed_snapshot->max_health) &&
           world.isBuildingComplete(building_id.value()) &&
           saw_completion_event;
}

bool test_rts_world_units_can_damage_and_repair_buildings() {
    RtsWorld world(18, 18, 1.0f);
    if (!world.registerUnitArchetype("worker", RtsUnitArchetype{
            2.3f, 0.35f, 3.0f, 1.5f, 1.0f, 55.0f, 4.0f, 1.0f, 7.0f, {}, 0.0f, 1, true, 10, 5, 0.2f
        }) ||
        !world.registerUnitArchetype("soldier", RtsUnitArchetype{
            2.4f, 0.35f, 4.0f, 1.5f, 2.1f, 80.0f, 24.0f, 0.5f, 9.0f
        }) ||
        !world.registerBuildingArchetype("outpost", RtsBuildingArchetype{
            BuildingDefinition{2, 2, true, true},
            true,
            false,
            0.0f,
            0.0f,
            0.0f,
            0.0f,
            {},
            {},
            2,
            false,
            180.0f,
            0.0f,
            30.0f
        })) {
        return false;
    }

    const std::optional<std::uint32_t> building_id =
        world.placeBuildingFromArchetype(0, "outpost", GridCoord{8, 8});
    if (!building_id.has_value() ||
        !world.addUnitFromArchetype(500, 0, glm::vec3(7.5f, 0.0f, 7.5f), "worker") ||
        !world.addUnitFromArchetype(600, 1, glm::vec3(5.5f, 0.0f, 9.5f), "soldier")) {
        return false;
    }

    if (!world.issueOrder(600, RtsOrder{
            RtsOrderType::attack_move,
            world.getBuildingSnapshot(building_id.value())->center,
            glm::vec3(0.0f),
            0,
            2.4f,
            0.1f,
            0,
            building_id.value()
        })) {
        return false;
    }

    bool saw_damage = false;
    for (int step = 0; step < 60; ++step) {
        world.update(0.1f);
        saw_damage |= has_event_type(world.events(), RtsEventType::building_damaged);
        if (world.buildingHealth(building_id.value()) < 180.0f) {
            break;
        }
    }
    const float damaged_health = world.buildingHealth(building_id.value());
    if (!saw_damage || damaged_health >= 180.0f) {
        return false;
    }

    if (!world.removeUnit(600) || !world.issueRepairOrder(500, building_id.value())) {
        return false;
    }

    bool saw_repair = false;
    for (int step = 0; step < 80; ++step) {
        world.update(0.1f);
        saw_repair |= has_event_type(world.events(), RtsEventType::building_repaired);
        if (nearly_equal(world.buildingHealth(building_id.value()), 180.0f, 0.05f)) {
            break;
        }
    }

    return saw_repair &&
           world.buildingHealth(building_id.value()) > damaged_health &&
           nearly_equal(world.buildingHealth(building_id.value()), 180.0f, 0.1f);
}

bool test_rts_world_towers_can_destroy_buildings() {
    RtsWorld world(18, 18, 1.0f);
    if (!world.registerBuildingArchetype("tower", RtsBuildingArchetype{
            BuildingDefinition{2, 2, true, true},
            true,
            true,
            5.0f,
            28.0f,
            0.5f,
            8.0f,
            {},
            {},
            0,
            false,
            160.0f,
            0.0f,
            0.0f
        }) ||
        !world.registerBuildingArchetype("outpost", RtsBuildingArchetype{
            BuildingDefinition{2, 2, true, true},
            true,
            false,
            0.0f,
            0.0f,
            0.0f,
            0.0f,
            {},
            {},
            0,
            false,
            90.0f,
            0.0f,
            0.0f
        })) {
        return false;
    }

    const std::optional<std::uint32_t> tower_id =
        world.placeBuildingFromArchetype(0, "tower", GridCoord{5, 5});
    const std::optional<std::uint32_t> target_id =
        world.placeBuildingFromArchetype(1, "outpost", GridCoord{8, 5});
    if (!tower_id.has_value() || !target_id.has_value()) {
        return false;
    }

    bool saw_damage = false;
    bool saw_destroyed = false;
    for (int step = 0; step < 80; ++step) {
        world.update(0.1f);
        saw_damage |= has_event_type(world.events(), RtsEventType::building_damaged);
        saw_destroyed |= has_event_type(world.events(), RtsEventType::building_destroyed);
        if (!world.getBuildingSnapshot(target_id.value()).has_value()) {
            break;
        }
    }

    return saw_damage &&
           saw_destroyed &&
           world.getBuildingSnapshot(tower_id.value()).has_value() &&
           !world.getBuildingSnapshot(target_id.value()).has_value();
}
}  // namespace

int main() {
    int failures = 0;

    if (!test_terrain_grid_queries()) {
        std::cerr << "RtsWorld test failure: terrain grid queries\n";
        ++failures;
    }

    if (!test_building_placement_rules()) {
        std::cerr << "RtsWorld test failure: building placement rules\n";
        ++failures;
    }

    if (!test_pathfinder_routes_around_blockers()) {
        std::cerr << "RtsWorld test failure: pathfinder routes around blockers\n";
        ++failures;
    }

    if (!test_pathfinder_prefers_lower_cost_terrain()) {
        std::cerr << "RtsWorld test failure: pathfinder prefers lower cost terrain\n";
        ++failures;
    }

    if (!test_rts_world_move_queue()) {
        std::cerr << "RtsWorld test failure: move queue\n";
        ++failures;
    }

    if (!test_rts_world_hold_and_stop_orders()) {
        std::cerr << "RtsWorld test failure: hold and stop orders\n";
        ++failures;
    }

    if (!test_rts_world_patrol_order()) {
        std::cerr << "RtsWorld test failure: patrol order\n";
        ++failures;
    }

    if (!test_rts_world_guard_order()) {
        std::cerr << "RtsWorld test failure: guard order\n";
        ++failures;
    }

    if (!test_rts_world_attack_move_order()) {
        std::cerr << "RtsWorld test failure: attack-move order\n";
        ++failures;
    }

    if (!test_rts_world_hold_position_attacks_without_moving()) {
        std::cerr << "RtsWorld test failure: hold-position combat\n";
        ++failures;
    }

    if (!test_rts_world_tower_auto_attack_and_win_condition()) {
        std::cerr << "RtsWorld test failure: tower auto-attack and win condition\n";
        ++failures;
    }

    if (!test_rts_world_archetypes_and_building_snapshots()) {
        std::cerr << "RtsWorld test failure: archetypes and building snapshots\n";
        ++failures;
    }

    if (!test_rts_world_formation_order_api()) {
        std::cerr << "RtsWorld test failure: formation order API\n";
        ++failures;
    }

    if (!test_rts_world_combat_event_output()) {
        std::cerr << "RtsWorld test failure: combat event output\n";
        ++failures;
    }

    if (!test_rts_world_resource_economy_api()) {
        std::cerr << "RtsWorld test failure: resource economy API\n";
        ++failures;
    }

    if (!test_rts_world_production_api()) {
        std::cerr << "RtsWorld test failure: production API\n";
        ++failures;
    }

    if (!test_rts_world_worker_harvest_and_deposit()) {
        std::cerr << "RtsWorld test failure: worker harvest and deposit\n";
        ++failures;
    }

    if (!test_rts_world_enemy_ai_harvests_produces_and_attacks()) {
        std::cerr << "RtsWorld test failure: enemy ai\n";
        ++failures;
    }

    if (!test_rts_world_enemy_ai_scouts_when_under_attack_threshold()) {
        std::cerr << "RtsWorld test failure: enemy ai scouting\n";
        ++failures;
    }

    if (!test_rts_world_enemy_ai_prioritizes_combat_targets()) {
        std::cerr << "RtsWorld test failure: enemy ai target priority\n";
        ++failures;
    }

    if (!test_rts_world_enemy_ai_retreats_wounded_units()) {
        std::cerr << "RtsWorld test failure: enemy ai retreat\n";
        ++failures;
    }

    if (!test_rts_world_enemy_ai_flanks_large_attack_waves()) {
        std::cerr << "RtsWorld test failure: enemy ai flanking\n";
        ++failures;
    }

    if (!test_rts_world_building_construction_and_cancel()) {
        std::cerr << "RtsWorld test failure: building construction and cancel\n";
        ++failures;
    }

    if (!test_rts_world_units_can_damage_and_repair_buildings()) {
        std::cerr << "RtsWorld test failure: unit building combat and repair\n";
        ++failures;
    }

    if (!test_rts_world_towers_can_destroy_buildings()) {
        std::cerr << "RtsWorld test failure: tower building combat\n";
        ++failures;
    }

    if (failures == 0) {
        std::cout << "RtsWorld tests passed\n";
        return 0;
    }
    return 1;
}
