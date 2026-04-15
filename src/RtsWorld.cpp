#include "RtsWorld.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace {
constexpr float kMinMoveSpeed = 0.0001f;
constexpr float kMinArrivalRadius = 0.01f;
constexpr float kMinAttackDamage = 0.01f;
constexpr float kMinAttackCooldown = 0.01f;
constexpr float kMinProjectileSpeed = 0.01f;
constexpr float kDefaultUnitHealth = 100.0f;
constexpr float kDefaultUnitDamage = 18.0f;
constexpr float kDefaultUnitAttackCooldown = 0.8f;
constexpr float kDefaultUnitProjectileSpeed = 8.5f;
constexpr float kRecentHitDuration = 0.16f;
constexpr float kMinBuildingHealth = 1.0f;
constexpr float kMinBuildTime = 0.01f;
constexpr float kConstructionStartHealthRatio = 0.1f;

float planar_distance(const glm::vec3& a, const glm::vec3& b) {
    return glm::length(glm::vec2(a.x - b.x, a.z - b.z));
}

glm::vec3 snapped_target_position(const TerrainGrid& terrain,
                                  const GridCoord& resolved_cell,
                                  const glm::vec3& original_target,
                                  bool original_cell_valid,
                                  const GridCoord& original_cell) {
    // if the original target already fell inside the resolved cell keep its x and z for smoother endpoints
    if (original_cell_valid &&
        original_cell.x == resolved_cell.x &&
        original_cell.y == resolved_cell.y) {
        glm::vec3 target = original_target;
        target.y = terrain.elevation(resolved_cell);
        return target;
    }
    return terrain.cellCenter(resolved_cell);
}
}  // namespace

RtsWorld::RtsWorld(int width,
                   int height,
                   float cell_size,
                   const glm::vec2& origin_xz)
    // construct subsystems in dependency order
    : terrain_(width, height, cell_size, origin_xz),
      fog_(terrain_.width(), terrain_.height(), 2),
      buildings_(terrain_.width(), terrain_.height()),
      pathfinder_(),
      unit_archetypes_(),
      building_archetypes_(),
      economy_(),
      units_(),
      owned_buildings_(),
      production_(),
      ai_(),
      combat_(),
      combat_events_(),
      next_generated_unit_id_(1),
      last_reported_winner_(std::nullopt) {}

TerrainGrid& RtsWorld::terrain() {
    return terrain_;
}

const TerrainGrid& RtsWorld::terrain() const {
    return terrain_;
}

BuildingSystem& RtsWorld::buildings() {
    return buildings_;
}

const BuildingSystem& RtsWorld::buildings() const {
    return buildings_;
}

bool RtsWorld::registerUnitArchetype(const std::string& archetype_id,
                                     const RtsUnitArchetype& archetype) {
    // validate archetypes up front so later runtime code can assume sane numbers
    if (archetype_id.empty() ||
        archetype.move_speed < kMinMoveSpeed ||
        archetype.radius <= 0.0f ||
        archetype.aggro_range <= 0.0f ||
        archetype.guard_radius <= 0.0f ||
        archetype.attack_range <= 0.0f ||
        archetype.max_health <= 0.0f ||
        archetype.attack_damage < kMinAttackDamage ||
        archetype.attack_cooldown < kMinAttackCooldown ||
        archetype.projectile_speed < kMinProjectileSpeed ||
        archetype.production_time < 0.0f ||
        archetype.supply_cost < 0 ||
        archetype.carry_capacity < 0 ||
        archetype.harvest_amount < 0 ||
        archetype.harvest_cooldown < 0.0f ||
        (archetype.can_harvest &&
         (archetype.carry_capacity <= 0 ||
          archetype.harvest_amount <= 0 ||
          archetype.harvest_cooldown <= 0.0f))) {
        return false;
    }

    for (const RtsResourceCost& cost : archetype.cost) {
        if (cost.resource_id.empty() || cost.amount < 0) {
            return false;
        }
    }

    unit_archetypes_[archetype_id] = archetype;
    return true;
}

const RtsUnitArchetype* RtsWorld::findUnitArchetype(const std::string& archetype_id) const {
    const auto it = unit_archetypes_.find(archetype_id);
    return it == unit_archetypes_.end() ? nullptr : &it->second;
}

bool RtsWorld::registerBuildingArchetype(const std::string& archetype_id,
                                         const RtsBuildingArchetype& archetype) {
    // buildings that also register towers must provide valid combat stats too
    if (archetype_id.empty() ||
        archetype.placement.footprint_width <= 0 ||
        archetype.placement.footprint_height <= 0 ||
        archetype.supply_provided < 0 ||
        archetype.max_health < kMinBuildingHealth ||
        archetype.build_time < 0.0f ||
        archetype.repair_rate < 0.0f ||
        (archetype.registers_tower &&
         (archetype.attack_range <= 0.0f ||
          archetype.attack_damage < kMinAttackDamage ||
          archetype.attack_cooldown < kMinAttackCooldown ||
          archetype.projectile_speed < kMinProjectileSpeed))) {
        return false;
    }

    for (const RtsResourceCost& cost : archetype.cost) {
        if (cost.resource_id.empty() || cost.amount < 0) {
            return false;
        }
    }

    building_archetypes_[archetype_id] = archetype;
    return true;
}

const RtsBuildingArchetype* RtsWorld::findBuildingArchetype(const std::string& archetype_id) const {
    const auto it = building_archetypes_.find(archetype_id);
    return it == building_archetypes_.end() ? nullptr : &it->second;
}

bool RtsWorld::addUnit(std::uint32_t unit_id,
                       int team,
                       const glm::vec3& position,
                       float move_speed,
                       float radius,
                       float aggro_range,
                       float guard_radius,
                       float attack_range) {
    if (units_.find(unit_id) != units_.end() ||
        move_speed < kMinMoveSpeed ||
        radius <= 0.0f ||
        aggro_range <= 0.0f ||
        guard_radius <= 0.0f ||
        attack_range <= 0.0f) {
        return false;
    }

    GridCoord start_cell{};
    if (!terrain_.worldToCell(position, start_cell) || !isCellTraversable(start_cell)) {
        return false;
    }

    UnitState unit{};
    unit.unit_id = unit_id;
    unit.team = team;
    unit.archetype_id = {};
    unit.position = terrain_.cellCenter(start_cell);
    unit.position.x = position.x;
    unit.position.z = position.z;
    unit.position.y = terrain_.elevation(start_cell);
    unit.move_speed = move_speed;
    unit.radius = radius;
    unit.aggro_range = aggro_range;
    unit.guard_radius = guard_radius;
    unit.attack_range = attack_range;
    unit.max_health = kDefaultUnitHealth;
    unit.health = kDefaultUnitHealth;
    unit.attack_damage = kDefaultUnitDamage;
    unit.attack_cooldown = kDefaultUnitAttackCooldown;
    unit.attack_cooldown_remaining = 0.0f;
    unit.projectile_speed = kDefaultUnitProjectileSpeed;
    unit.harvest_cooldown = 0.0f;
    unit.harvest_cooldown_remaining = 0.0f;
    unit.recent_hit_timer = 0.0f;
    unit.carried_resource_id = {};
    unit.carried_resource_amount = 0;
    unit.moving = false;
    unit.holding_position = false;
    unit.order_queue = {};
    unit.active_order = std::nullopt;
    unit.path_points = {};
    unit.path_index = 0;
    unit.goal_cell = start_cell;
    unit.has_goal_cell = false;

    units_[unit_id] = unit;
    next_generated_unit_id_ = std::max(next_generated_unit_id_, unit_id + 1);
    return true;
}

bool RtsWorld::addUnitFromArchetype(std::uint32_t unit_id,
                                    int team,
                                    const glm::vec3& position,
                                    const std::string& archetype_id) {
    const RtsUnitArchetype* archetype = findUnitArchetype(archetype_id);
    if (!archetype) {
        return false;
    }
    if (!addUnit(unit_id,
                 team,
                 position,
                 archetype->move_speed,
                 archetype->radius,
                 archetype->aggro_range,
                 archetype->guard_radius,
                 archetype->attack_range)) {
        return false;
    }

    UnitState* unit = findUnit(unit_id);
    if (!unit) {
        return false;
    }
    unit->archetype_id = archetype_id;
    unit->max_health = archetype->max_health;
    unit->health = archetype->max_health;
    unit->attack_damage = archetype->attack_damage;
    unit->attack_cooldown = archetype->attack_cooldown;
    unit->attack_cooldown_remaining = 0.0f;
    unit->projectile_speed = archetype->projectile_speed;
    unit->harvest_cooldown = archetype->harvest_cooldown;
    unit->harvest_cooldown_remaining = 0.0f;
    return true;
}

bool RtsWorld::removeUnit(std::uint32_t unit_id) {
    const UnitState* unit = findUnit(unit_id);
    if (!unit) {
        return false;
    }
    if (unit->carried_resource_amount > 0 && !unit->carried_resource_id.empty()) {
        addTeamResourceAmount(unit->team, unit->carried_resource_id, unit->carried_resource_amount);
    }
    units_.erase(unit_id);
    return true;
}

bool RtsWorld::hasUnit(std::uint32_t unit_id) const {
    return findUnit(unit_id) != nullptr;
}

std::size_t RtsWorld::unitCount() const {
    return units_.size();
}

glm::vec3 RtsWorld::getUnitPosition(std::uint32_t unit_id) const {
    const UnitState* unit = findUnit(unit_id);
    return unit ? unit->position : glm::vec3(0.0f);
}

float RtsWorld::unitHealth(std::uint32_t unit_id) const {
    const UnitState* unit = findUnit(unit_id);
    return unit ? unit->health : 0.0f;
}

bool RtsWorld::isUnitAlive(std::uint32_t unit_id) const {
    const UnitState* unit = findUnit(unit_id);
    return unit && unit->health > 0.0f;
}

std::optional<RtsWorldUnitSnapshot> RtsWorld::getUnitSnapshot(std::uint32_t unit_id) const {
    const UnitState* unit = findUnit(unit_id);
    if (!unit) {
        return std::nullopt;
    }

    return RtsWorldUnitSnapshot{
        unit->unit_id,
        unit->team,
        unit->archetype_id,
        unit->position,
        unit->radius,
        unit->health,
        unit->max_health,
        unit->moving,
        unit->holding_position,
        unit->order_queue.size(),
        unit->active_order.has_value() ? std::optional<RtsOrderType>(unit->active_order->type)
                                       : std::nullopt,
        unit->recent_hit_timer,
        unit->carried_resource_id,
        unit->carried_resource_amount
    };
}

std::vector<RtsWorldUnitSnapshot> RtsWorld::unitSnapshots() const {
    std::vector<RtsWorldUnitSnapshot> snapshots{};
    snapshots.reserve(units_.size());
    for (const auto& entry : units_) {
        const UnitState& unit = entry.second;
        snapshots.push_back(RtsWorldUnitSnapshot{
            unit.unit_id,
            unit.team,
            unit.archetype_id,
            unit.position,
            unit.radius,
            unit.health,
            unit.max_health,
            unit.moving,
            unit.holding_position,
            unit.order_queue.size(),
            unit.active_order.has_value() ? std::optional<RtsOrderType>(unit.active_order->type)
                                          : std::nullopt,
            unit.recent_hit_timer,
            unit.carried_resource_id,
            unit.carried_resource_amount
        });
    }
    std::sort(snapshots.begin(), snapshots.end(),
              [](const RtsWorldUnitSnapshot& lhs, const RtsWorldUnitSnapshot& rhs) {
                  return lhs.unit_id < rhs.unit_id;
              });
    return snapshots;
}

bool RtsWorld::isUnitMoving(std::uint32_t unit_id) const {
    const UnitState* unit = findUnit(unit_id);
    return unit ? unit->moving : false;
}

bool RtsWorld::isHoldingPosition(std::uint32_t unit_id) const {
    const UnitState* unit = findUnit(unit_id);
    return unit ? unit->holding_position : false;
}

std::size_t RtsWorld::queuedOrderCount(std::uint32_t unit_id) const {
    const UnitState* unit = findUnit(unit_id);
    return unit ? unit->order_queue.size() : 0;
}

std::optional<RtsOrderType> RtsWorld::activeOrderType(std::uint32_t unit_id) const {
    const UnitState* unit = findUnit(unit_id);
    if (!unit || !unit->active_order.has_value()) {
        return std::nullopt;
    }
    return unit->active_order->type;
}

bool RtsWorld::canPlaceBuildingFromArchetype(const std::string& archetype_id,
                                             const GridCoord& anchor) const {
    const RtsBuildingArchetype* archetype = findBuildingArchetype(archetype_id);
    if (!archetype) {
        return false;
    }
    return buildings_.canPlaceBuilding(terrain_, archetype->placement, anchor);
}

std::optional<std::uint32_t> RtsWorld::placeBuildingFromArchetype(int team,
                                                                  const std::string& archetype_id,
                                                                  const GridCoord& anchor) {
    const RtsBuildingArchetype* archetype = findBuildingArchetype(archetype_id);
    if (!archetype) {
        return std::nullopt;
    }

    const std::optional<std::uint32_t> building_id =
        buildings_.placeBuilding(terrain_, archetype->placement, anchor);
    if (!building_id.has_value()) {
        return std::nullopt;
    }

    // placing an already finished building skips the worker construction loop entirely
    // this path is mostly for setup code tests and instant placement behaviors
    owned_buildings_[building_id.value()] = OwnedBuildingState{
        building_id.value(),
        team,
        archetype_id,
        archetype->max_health,
        archetype->max_health,
        1.0f,
        archetype->build_time,
        archetype->repair_rate,
        false,
        archetype->cost
    };
    production_.registerBuilding(building_id.value(),
                                 buildingCenter(*buildings_.findBuilding(building_id.value())));
    combat_events_.push_back(RtsCombatEvent{
        RtsCombatEventType::building_placed,
        0,
        0,
        0,
        building_id.value(),
        team,
        -1,
        buildingCenter(*buildings_.findBuilding(building_id.value())),
        false
    });

    if (archetype->registers_tower) {
        // completed tower buildings register their autonomous combat behavior right away
        addTower(building_id.value(),
                 building_id.value(),
                 team,
                 archetype->attack_range,
                 archetype->attack_damage,
                 archetype->attack_cooldown,
                 archetype->projectile_speed);
    }
    return building_id;
}

bool RtsWorld::removeBuilding(std::uint32_t building_id) {
    const BuildingInstance* building = buildings_.findBuilding(building_id);
    if (!building) {
        return false;
    }

    const glm::vec3 center = buildingCenter(*building);
    int team = -1;
    const auto owned_it = owned_buildings_.find(building_id);
    if (owned_it != owned_buildings_.end()) {
        team = owned_it->second.team;
    }

    clearProductionQueue(building_id, true);
    production_.unregisterBuilding(building_id);
    removeTower(building_id);
    if (!buildings_.removeBuilding(building_id)) {
        return false;
    }
    owned_buildings_.erase(building_id);
    combat_events_.push_back(RtsCombatEvent{
        RtsCombatEventType::building_removed,
        0,
        0,
        0,
        building_id,
        team,
        -1,
        center,
        false
    });
    return true;
}

std::optional<RtsWorldBuildingSnapshot> RtsWorld::getBuildingSnapshot(std::uint32_t building_id) const {
    const OwnedBuildingState* owned = findOwnedBuilding(building_id);
    const BuildingInstance* building = buildings_.findBuilding(building_id);
    if (!owned || !building) {
        return std::nullopt;
    }

    const RtsBuildingArchetype* archetype = findBuildingArchetype(owned->archetype_id);
    return RtsWorldBuildingSnapshot{
        owned->building_id,
        owned->team,
        owned->archetype_id,
        building->anchor,
        building->footprint_width,
        building->footprint_height,
        buildingCenter(*building),
        building->blocks_movement,
        archetype ? archetype->counts_for_victory : true,
        archetype ? archetype->registers_tower : false,
        owned->health,
        owned->max_health,
        owned->build_progress,
        owned->under_construction,
        !owned->under_construction
    };
}

float RtsWorld::buildingHealth(std::uint32_t building_id) const {
    const OwnedBuildingState* building = findOwnedBuilding(building_id);
    return building ? building->health : 0.0f;
}

bool RtsWorld::isBuildingComplete(std::uint32_t building_id) const {
    const OwnedBuildingState* building = findOwnedBuilding(building_id);
    return building && !building->under_construction;
}

std::optional<std::uint32_t> RtsWorld::startBuildingConstruction(
    int team,
    const std::string& archetype_id,
    const GridCoord& anchor,
    std::uint32_t builder_unit_id,
    bool spend_resources) {
    const RtsBuildingArchetype* archetype = findBuildingArchetype(archetype_id);
    UnitState* builder = findUnit(builder_unit_id);
    if (!archetype || !builder || builder->team != team) {
        return std::nullopt;
    }

    const RtsUnitArchetype* builder_archetype = findUnitArchetype(builder->archetype_id);
    if (!builder_archetype || !builder_archetype->can_harvest) {
        return std::nullopt;
    }
    if (spend_resources && !spendTeamResources(team, archetype->cost)) {
        return std::nullopt;
    }

    // reserve the footprint immediately so nothing else can path through or build on the site while it is unfinished
    const std::optional<std::uint32_t> building_id =
        buildings_.placeBuilding(terrain_, archetype->placement, anchor);
    if (!building_id.has_value()) {
        if (spend_resources) {
            refundTeamResources(team, archetype->cost);
        }
        return std::nullopt;
    }

    const float initial_progress = archetype->build_time <= 0.0f ? 1.0f : 0.0f;
    const float initial_health =
        archetype->build_time <= 0.0f
            ? archetype->max_health
            : std::max(kMinBuildingHealth,
                       archetype->max_health * kConstructionStartHealthRatio);
    // unfinished buildings still get some health so they can exist as attackable world objects during construction
    owned_buildings_[building_id.value()] = OwnedBuildingState{
        building_id.value(),
        team,
        archetype_id,
        initial_health,
        archetype->max_health,
        initial_progress,
        archetype->build_time,
        archetype->repair_rate,
        archetype->build_time > 0.0f,
        archetype->cost
    };

    const BuildingInstance* building = buildings_.findBuilding(building_id.value());
    const glm::vec3 center = building ? buildingCenter(*building) : glm::vec3(0.0f);
    combat_events_.push_back(RtsCombatEvent{
        RtsCombatEventType::building_placed,
        builder_unit_id,
        0,
        0,
        building_id.value(),
        team,
        -1,
        center,
        false
    });

    if (archetype->build_time <= 0.0f) {
        finishBuildingConstruction(owned_buildings_.at(building_id.value()));
        return building_id;
    }

    combat_events_.push_back(RtsCombatEvent{
        RtsCombatEventType::construction_started,
        builder_unit_id,
        0,
        0,
        building_id.value(),
        team,
        -1,
        center,
        false,
        {},
        0,
        archetype_id
    });

    if (!issueOrder(builder_unit_id, RtsOrder{
            RtsOrderType::construct,
            center,
            glm::vec3(0.0f),
            0,
            builder->move_speed,
            builder->radius + 0.2f,
            0,
            building_id.value()
        })) {
        // if the worker cannot actually accept the construct order undo the whole placement
        cancelBuildingConstruction(building_id.value(), spend_resources);
        return std::nullopt;
    }
    return building_id;
}

bool RtsWorld::cancelBuildingConstruction(std::uint32_t building_id, bool refund_resources) {
    OwnedBuildingState* building = findOwnedBuilding(building_id);
    const BuildingInstance* building_instance = buildings_.findBuilding(building_id);
    if (!building || !building_instance || !building->under_construction) {
        return false;
    }

    const glm::vec3 center = buildingCenter(*building_instance);
    // clear any workers that were still pointing at this site so they do not keep a dangling target id
    for (auto& entry : units_) {
        UnitState& unit = entry.second;
        if (unit.active_order.has_value() &&
            unit.active_order->target_building_id == building_id &&
            (unit.active_order->type == RtsOrderType::construct ||
             unit.active_order->type == RtsOrderType::repair)) {
            unit.active_order = std::nullopt;
            clearUnitMotion(unit);
        }
    }

    if (refund_resources) {
        refundTeamResources(building->team, building->cost);
    }
    combat_events_.push_back(RtsCombatEvent{
        RtsCombatEventType::construction_canceled,
        0,
        0,
        0,
        building_id,
        building->team,
        -1,
        center,
        false,
        {},
        0,
        building->archetype_id
    });
    return removeBuilding(building_id);
}

bool RtsWorld::issueRepairOrder(std::uint32_t unit_id,
                                std::uint32_t building_id,
                                bool queue_when_busy) {
    const BuildingInstance* building = buildings_.findBuilding(building_id);
    const UnitState* unit = findUnit(unit_id);
    if (!building || !unit) {
        return false;
    }
    const glm::vec3 center = buildingCenter(*building);
    return issueOrder(unit_id, RtsOrder{
        RtsOrderType::repair,
        center,
        glm::vec3(0.0f),
        0,
        unit->move_speed,
        unit->radius + 0.2f,
        0,
        building_id
    }, queue_when_busy);
}

std::vector<RtsWorldBuildingSnapshot> RtsWorld::buildingSnapshots() const {
    std::vector<RtsWorldBuildingSnapshot> snapshots{};
    snapshots.reserve(owned_buildings_.size());
    for (const auto& entry : owned_buildings_) {
        const OwnedBuildingState& owned = entry.second;
        const BuildingInstance* building = buildings_.findBuilding(owned.building_id);
        if (!building) {
            continue;
        }
        const RtsBuildingArchetype* archetype = findBuildingArchetype(owned.archetype_id);
        snapshots.push_back(RtsWorldBuildingSnapshot{
            owned.building_id,
            owned.team,
            owned.archetype_id,
            building->anchor,
            building->footprint_width,
            building->footprint_height,
            buildingCenter(*building),
            building->blocks_movement,
            archetype ? archetype->counts_for_victory : true,
            archetype ? archetype->registers_tower : false,
            owned.health,
            owned.max_health,
            owned.build_progress,
            owned.under_construction,
            !owned.under_construction
        });
    }
    std::sort(snapshots.begin(), snapshots.end(),
              [](const RtsWorldBuildingSnapshot& lhs, const RtsWorldBuildingSnapshot& rhs) {
                  return lhs.building_id < rhs.building_id;
              });
    return snapshots;
}

bool RtsWorld::addTower(std::uint32_t tower_id,
                        std::uint32_t building_id,
                        int team,
                        float attack_range,
                        float attack_damage,
                        float attack_cooldown,
                        float projectile_speed) {
    if (!buildings_.findBuilding(building_id)) {
        return false;
    }
    const OwnedBuildingState* owned = findOwnedBuilding(building_id);
    if (owned && owned->under_construction) {
        return false;
    }
    return combat_.addTower(tower_id,
                            building_id,
                            team,
                            attack_range,
                            attack_damage,
                            attack_cooldown,
                            projectile_speed);
}

bool RtsWorld::removeTower(std::uint32_t tower_id) {
    return combat_.removeTower(tower_id);
}

bool RtsWorld::hasTower(std::uint32_t tower_id) const {
    return combat_.hasTower(tower_id);
}

std::size_t RtsWorld::towerCount() const {
    return combat_.towerCount();
}

std::vector<RtsWorldProjectileSnapshot> RtsWorld::projectileSnapshots() const {
    return combat_.projectileSnapshots([this](const RtsCombatSystem::ProjectileState& projectile)
                                       -> std::optional<glm::vec3> {
        if (projectile.target_unit_id != 0) {
            const UnitState* target = findUnit(projectile.target_unit_id);
            return target ? std::optional<glm::vec3>(target->position) : std::nullopt;
        }
        if (projectile.target_building_id != 0) {
            const BuildingInstance* building = buildings_.findBuilding(projectile.target_building_id);
            return building ? std::optional<glm::vec3>(buildingCenter(*building)) : std::nullopt;
        }
        return std::nullopt;
    });
}

const std::vector<RtsEvent>& RtsWorld::events() const {
    return combat_events_;
}

void RtsWorld::setTeamResourceAmount(int team,
                                     const std::string& resource_id,
                                     int amount) {
    economy_.setTeamResourceAmount(team, resource_id, amount);
    emitResourceChangedEvent(team, resource_id, economy_.teamResourceAmount(team, resource_id));
}

int RtsWorld::addTeamResourceAmount(int team,
                                    const std::string& resource_id,
                                    int delta) {
    const int amount = economy_.addTeamResourceAmount(team, resource_id, delta);
    emitResourceChangedEvent(team, resource_id, amount);
    return amount;
}

int RtsWorld::teamResourceAmount(int team, const std::string& resource_id) const {
    return economy_.teamResourceAmount(team, resource_id);
}

std::vector<RtsTeamResourceSnapshot> RtsWorld::teamResourceSnapshots(int team) const {
    return economy_.teamResourceSnapshots(team);
}

bool RtsWorld::canAffordCosts(int team, const std::vector<RtsResourceCost>& costs) const {
    return economy_.canAffordCosts(team, costs);
}

bool RtsWorld::spendTeamResources(int team, const std::vector<RtsResourceCost>& costs) {
    if (!economy_.spendTeamResources(team, costs)) {
        return false;
    }
    for (const RtsResourceCost& cost : costs) {
        if (cost.amount > 0 && !cost.resource_id.empty()) {
            emitResourceChangedEvent(team,
                                     cost.resource_id,
                                     economy_.teamResourceAmount(team, cost.resource_id));
        }
    }
    return true;
}

void RtsWorld::refundTeamResources(int team, const std::vector<RtsResourceCost>& costs) {
    economy_.refundTeamResources(team, costs);
    for (const RtsResourceCost& cost : costs) {
        if (cost.amount > 0 && !cost.resource_id.empty()) {
            emitResourceChangedEvent(team,
                                     cost.resource_id,
                                     economy_.teamResourceAmount(team, cost.resource_id));
        }
    }
}

int RtsWorld::teamSupplyUsed(int team) const {
    int total = 0;
    for (const auto& entry : units_) {
        const UnitState& unit = entry.second;
        if (unit.team != team || unit.health <= 0.0f) {
            continue;
        }
        const RtsUnitArchetype* archetype = findUnitArchetype(unit.archetype_id);
        if (archetype) {
            total += archetype->supply_cost;
        }
    }
    return total;
}

int RtsWorld::teamSupplyProvided(int team) const {
    int total = 0;
    for (const auto& entry : owned_buildings_) {
        const OwnedBuildingState& building = entry.second;
        if (building.team != team ||
            building.under_construction ||
            !buildings_.findBuilding(building.building_id)) {
            continue;
        }
        const RtsBuildingArchetype* archetype = findBuildingArchetype(building.archetype_id);
        if (archetype) {
            total += archetype->supply_provided;
        }
    }
    return total;
}

std::optional<std::uint32_t> RtsWorld::addResourceNode(const std::string& resource_id,
                                                       const GridCoord& cell,
                                                       int amount) {
    return economy_.addResourceNode(terrain_, buildings_, resource_id, cell, amount);
}

bool RtsWorld::removeResourceNode(std::uint32_t node_id) {
    return economy_.removeResourceNode(node_id);
}

int RtsWorld::resourceNodeAmount(std::uint32_t node_id) const {
    return economy_.resourceNodeAmount(node_id);
}

int RtsWorld::harvestResourceNode(std::uint32_t node_id, int requested_amount) {
    return economy_.harvestResourceNode(node_id, requested_amount);
}

std::vector<RtsWorldResourceNodeSnapshot> RtsWorld::resourceNodeSnapshots() const {
    return economy_.resourceNodeSnapshots(terrain_);
}

bool RtsWorld::canProduceUnitFromBuilding(std::uint32_t building_id,
                                          const std::string& unit_archetype_id) const {
    const auto owned_it = owned_buildings_.find(building_id);
    if (owned_it == owned_buildings_.end()) {
        return false;
    }
    if (!buildings_.findBuilding(building_id)) {
        return false;
    }

    const OwnedBuildingState& owned = owned_it->second;
    if (owned.under_construction) {
        return false;
    }
    const RtsUnitArchetype* unit_archetype = findUnitArchetype(unit_archetype_id);
    if (!unit_archetype || unit_archetype->production_time <= 0.0f) {
        return false;
    }
    if (!buildingCanProduceUnit(owned, unit_archetype_id)) {
        return false;
    }
    if (!canAffordCosts(owned.team, unit_archetype->cost)) {
        return false;
    }

    const int queued_supply =
        production_.queuedSupplyForTeam(owned.team,
                                        [this](std::uint32_t queued_building_id) {
                                            const auto it = owned_buildings_.find(queued_building_id);
                                            return it == owned_buildings_.end() ? -1 : it->second.team;
                                        },
                                        [this](const std::string& queued_unit_archetype_id) {
                                            const RtsUnitArchetype* queued_archetype =
                                                findUnitArchetype(queued_unit_archetype_id);
                                            return queued_archetype ? queued_archetype->supply_cost : 0;
                                        });

    return teamSupplyUsed(owned.team) + queued_supply + unit_archetype->supply_cost <=
           teamSupplyProvided(owned.team);
}

bool RtsWorld::enqueueProduction(std::uint32_t building_id, const std::string& unit_archetype_id) {
    auto owned_it = owned_buildings_.find(building_id);
    if (owned_it == owned_buildings_.end()) {
        return false;
    }
    const RtsUnitArchetype* unit_archetype = findUnitArchetype(unit_archetype_id);
    if (!unit_archetype || !canProduceUnitFromBuilding(building_id, unit_archetype_id)) {
        return false;
    }
    if (!spendTeamResources(owned_it->second.team, unit_archetype->cost)) {
        return false;
    }

    if (!production_.enqueueProduction(building_id,
                                       unit_archetype_id,
                                       unit_archetype->production_time)) {
        refundTeamResources(owned_it->second.team, unit_archetype->cost);
        return false;
    }
    combat_events_.push_back(RtsEvent{
        RtsEventType::production_started,
        0,
        0,
        0,
        building_id,
        owned_it->second.team,
        -1,
        buildingCenter(*buildings_.findBuilding(building_id)),
        false,
        {},
        0,
        unit_archetype_id
    });
    return true;
}

bool RtsWorld::cancelLastProduction(std::uint32_t building_id, bool refund_resources) {
    auto owned_it = owned_buildings_.find(building_id);
    if (owned_it == owned_buildings_.end()) {
        return false;
    }

    const std::optional<RtsProductionSystem::ProductionEntry> entry =
        production_.cancelLastProduction(building_id);
    if (!entry.has_value()) {
        return false;
    }
    if (refund_resources) {
        if (const RtsUnitArchetype* unit_archetype = findUnitArchetype(entry->unit_archetype_id)) {
            refundTeamResources(owned_it->second.team, unit_archetype->cost);
        }
    }
    combat_events_.push_back(RtsEvent{
        RtsEventType::production_canceled,
        0,
        0,
        0,
        building_id,
        owned_it->second.team,
        -1,
        buildingCenter(*buildings_.findBuilding(building_id)),
        false,
        {},
        0,
        entry->unit_archetype_id
    });
    return true;
}

bool RtsWorld::clearProductionQueue(std::uint32_t building_id, bool refund_resources) {
    auto owned_it = owned_buildings_.find(building_id);
    if (owned_it == owned_buildings_.end()) {
        return false;
    }

    const std::vector<RtsProductionSystem::ProductionEntry> cleared =
        production_.clearProductionQueue(building_id);
    for (const RtsProductionSystem::ProductionEntry& entry : cleared) {
        if (refund_resources) {
            if (const RtsUnitArchetype* unit_archetype = findUnitArchetype(entry.unit_archetype_id)) {
                refundTeamResources(owned_it->second.team, unit_archetype->cost);
            }
        }
        combat_events_.push_back(RtsEvent{
            RtsEventType::production_canceled,
            0,
            0,
            0,
            building_id,
            owned_it->second.team,
            -1,
            buildingCenter(*buildings_.findBuilding(building_id)),
            false,
            {},
            0,
            entry.unit_archetype_id
        });
    }
    return true;
}

bool RtsWorld::setProductionRallyPoint(std::uint32_t building_id, const glm::vec3& rally_point) {
    return production_.setRallyPoint(building_id, rally_point);
}

glm::vec3 RtsWorld::productionRallyPoint(std::uint32_t building_id) const {
    return production_.rallyPoint(building_id);
}

std::vector<RtsWorldProductionSnapshot> RtsWorld::productionSnapshots() const {
    return production_.snapshots(
        [this](std::uint32_t building_id) {
            const auto it = owned_buildings_.find(building_id);
            return it == owned_buildings_.end() ? -1 : it->second.team;
        },
        [this](std::uint32_t building_id) {
            const auto it = owned_buildings_.find(building_id);
            return it == owned_buildings_.end() ? std::string() : it->second.archetype_id;
        });
}

bool RtsWorld::issueHarvestOrder(std::uint32_t unit_id, std::uint32_t resource_node_id) {
    const RtsEconomySystem::ResourceNodeState* node = economy_.findResourceNode(resource_node_id);
    if (!node) {
        return false;
    }
    return issueOrder(unit_id, RtsOrder{
        RtsOrderType::harvest,
        terrain_.cellCenter(node->cell),
        glm::vec3(0.0f),
        0,
        0.0f,
        0.15f,
        resource_node_id
    });
}

void RtsWorld::setAiProfile(int team, const RtsAiProfile& profile) {
    ai_.setTeamProfile(team, profile);
}

bool RtsWorld::removeAiProfile(int team) {
    return ai_.removeTeamProfile(team);
}

bool RtsWorld::hasAiProfile(int team) const {
    return ai_.hasTeamProfile(team);
}

const RtsAiProfile* RtsWorld::aiProfile(int team) const {
    return ai_.teamProfile(team);
}

const std::vector<RtsCombatEvent>& RtsWorld::combatEvents() const {
    return combat_events_;
}

const FogOfWar& RtsWorld::fog() const {
    return fog_;
}

VisibilityState RtsWorld::cellVisibilityForTeam(int team, const GridCoord& cell) const {
    return fog_.cellVisibility(team, cell);
}

bool RtsWorld::isUnitVisibleToTeam(int team, std::uint32_t unit_id) const {
    const UnitState* unit = findUnit(unit_id);
    if (!unit) {
        return false;
    }
    return fog_.isPositionVisible(team, unit->position, terrain_);
}

bool RtsWorld::isBuildingVisibleToTeam(int team, std::uint32_t building_id) const {
    const BuildingInstance* building = buildings_.findBuilding(building_id);
    if (!building) {
        return false;
    }
    const std::vector<GridCoord> cells = terrain_.cellsInFootprint(
        building->anchor, building->footprint_width, building->footprint_height);
    for (const GridCoord& cell : cells) {
        if (fog_.isVisible(team, cell)) {
            return true;
        }
    }
    return false;
}

void RtsWorld::updateFogOfWar() {
    for (int team = 0; team < 2; ++team) {
        fog_.clearCurrentVision(team);
    }

    for (const auto& entry : units_) {
        const UnitState& unit = entry.second;
        if (unit.team < 0 || unit.team >= 2) {
            continue;
        }
        GridCoord unit_cell{};
        if (!terrain_.worldToCell(unit.position, unit_cell)) {
            continue;
        }
        const RtsUnitArchetype* archetype = findUnitArchetype(unit.archetype_id);
        const float vision_range = archetype ? archetype->vision_range : 5.0f;
        const int radius_cells = static_cast<int>(vision_range / terrain_.cellSize() + 0.5f);
        fog_.revealCircle(unit.team, unit_cell, radius_cells);
    }

    for (const auto& entry : owned_buildings_) {
        const OwnedBuildingState& building = entry.second;
        if (building.team < 0 || building.team >= 2) {
            continue;
        }
        const BuildingInstance* instance = buildings_.findBuilding(building.building_id);
        if (!instance) {
            continue;
        }
        const RtsBuildingArchetype* archetype = findBuildingArchetype(building.archetype_id);
        const float vision_range = archetype ? archetype->vision_range : 6.0f;
        const int radius_cells = static_cast<int>(vision_range / terrain_.cellSize() + 0.5f);
        const GridCoord center_cell{
            instance->anchor.x + instance->footprint_width / 2,
            instance->anchor.y + instance->footprint_height / 2
        };
        fog_.revealCircle(building.team, center_cell, radius_cells);
    }
}

std::optional<int> RtsWorld::winningTeam() const {
    std::vector<int> active_teams{};
    auto add_team = [&active_teams](int team) {
        if (std::find(active_teams.begin(), active_teams.end(), team) == active_teams.end()) {
            active_teams.push_back(team);
        }
    };

    for (const auto& entry : units_) {
        if (entry.second.health > 0.0f) {
            add_team(entry.second.team);
        }
    }
    for (const auto& entry : owned_buildings_) {
        const BuildingInstance* building = buildings_.findBuilding(entry.first);
        const RtsBuildingArchetype* archetype = findBuildingArchetype(entry.second.archetype_id);
        if (building &&
            !entry.second.under_construction &&
            (!archetype || archetype->counts_for_victory)) {
            add_team(entry.second.team);
        }
    }
    for (const RtsCombatSystem::TowerState& tower : combat_.towerStates()) {
        if (buildings_.findBuilding(tower.building_id)) {
            add_team(tower.team);
        }
    }

    if (active_teams.size() == 1) {
        return active_teams.front();
    }
    return std::nullopt;
}

bool RtsWorld::isMatchOver() const {
    return winningTeam().has_value();
}

bool RtsWorld::issueOrder(std::uint32_t unit_id,
                          const RtsOrder& order,
                          bool queue_when_busy) {
    UnitState* unit = findUnit(unit_id);
    if (!unit || unit->health <= 0.0f) {
        return false;
    }

    // fill missing runtime values from the unit so callers can submit lightweight orders
    RtsOrder sanitized = order;
    sanitized.move_speed =
        sanitized.move_speed > 0.0f ? sanitized.move_speed : unit->move_speed;
    sanitized.arrival_radius = std::max(sanitized.arrival_radius, kMinArrivalRadius);

    switch (sanitized.type) {
    case RtsOrderType::move:
    case RtsOrderType::attack_move:
        // these orders only need a legal move speed because the target is positional
        if (sanitized.move_speed < kMinMoveSpeed) {
            return false;
        }
        break;
    case RtsOrderType::patrol:
        // patrol needs two distinct endpoints or it would collapse into a stationary loop
        if (sanitized.move_speed < kMinMoveSpeed ||
            planar_distance(sanitized.target_position, sanitized.secondary_target_position) <
                kMinArrivalRadius) {
            return false;
        }
        break;
    case RtsOrderType::guard:
        // guard needs a living friendly unit id to orbit around
        if (sanitized.target_unit_id == 0 || sanitized.move_speed < kMinMoveSpeed) {
            return false;
        }
        break;
    case RtsOrderType::harvest:
        // harvest is also movement driven but targets a resource node instead of a free position
        if (sanitized.target_resource_node_id == 0 || sanitized.move_speed < kMinMoveSpeed) {
            return false;
        }
        break;
    case RtsOrderType::construct:
    case RtsOrderType::repair: {
        const OwnedBuildingState* building = findOwnedBuilding(sanitized.target_building_id);
        const RtsUnitArchetype* unit_archetype = findUnitArchetype(unit->archetype_id);
        if (sanitized.target_building_id == 0 ||
            !building ||
            sanitized.move_speed < kMinMoveSpeed ||
            !unit_archetype ||
            !unit_archetype->can_harvest) {
            // the current design reuses worker units for both construction and repair tasks
            return false;
        }
        if (const BuildingInstance* building_instance = buildings_.findBuilding(sanitized.target_building_id)) {
            sanitized.target_position = buildingCenter(*building_instance);
        }
        break;
    }
    case RtsOrderType::stop:
        // stop clears current and queued intent immediately
        unit->holding_position = false;
        unit->order_queue.clear();
        unit->active_order = std::nullopt;
        clearUnitMotion(*unit);
        return true;
    case RtsOrderType::hold_position:
        // hold position also clears movement but leaves the unit ready to auto attack nearby enemies
        unit->holding_position = true;
        unit->order_queue.clear();
        unit->active_order = std::nullopt;
        clearUnitMotion(*unit);
        return true;
    }

    unit->holding_position = false;
    if (!queue_when_busy) {
        // replace the current command immediately
        // this is the common rts behavior for a fresh right click command
        unit->order_queue.clear();
        unit->active_order = sanitized;
        clearUnitMotion(*unit);
        return true;
    }

    if (!unit->active_order.has_value()) {
        // in queue mode start right away if the active slot is free
        // queueing only matters when something is already running
        unit->active_order = sanitized;
        clearUnitMotion(*unit);
        return true;
    }

    // otherwise append behind the current work
    // the unit will not see this order until pullNextQueuedOrder promotes it later
    unit->order_queue.push_back(sanitized);
    return true;
}

bool RtsWorld::issueFormationOrder(const std::vector<std::uint32_t>& unit_ids,
                                   const glm::vec3& center,
                                   RtsOrderType order_type,
                                   float spacing,
                                   bool queue_when_busy,
                                   std::uint32_t target_unit_id,
                                   std::uint32_t target_building_id) {
    std::vector<UnitState*> ordered_units{};
    ordered_units.reserve(unit_ids.size());
    for (const std::uint32_t unit_id : unit_ids) {
        UnitState* unit = findUnit(unit_id);
        if (!unit || unit->health <= 0.0f) {
            continue;
        }
        ordered_units.push_back(unit);
    }
    if (ordered_units.empty()) {
        return false;
    }

    if (order_type == RtsOrderType::stop || order_type == RtsOrderType::hold_position) {
        // stop and hold do not need formation geometry
        bool any_accepted = false;
        for (UnitState* unit : ordered_units) {
            any_accepted |= issueOrder(unit->unit_id, RtsOrder{
                order_type,
                center,
                center,
                0,
                unit->move_speed,
                unit->radius
            });
        }
        return any_accepted;
    }

    // sort into a stable order before assigning formation slots
    std::sort(ordered_units.begin(), ordered_units.end(),
              [](const UnitState* lhs, const UnitState* rhs) {
                  if (lhs->position.z == rhs->position.z) {
                      return lhs->position.x < rhs->position.x;
                  }
                  return lhs->position.z < rhs->position.z;
              });

    // lay the group out on a simple square-ish grid centered around the target point
    const std::size_t columns = static_cast<std::size_t>(
        std::ceil(std::sqrt(static_cast<float>(ordered_units.size()))));
    const std::size_t rows = (ordered_units.size() + columns - 1) / columns;
    const float start_x = -0.5f * static_cast<float>(columns - 1) * spacing;
    const float start_z = -0.5f * static_cast<float>(rows - 1) * spacing;

    bool any_accepted = false;
    for (std::size_t index = 0; index < ordered_units.size(); ++index) {
        const std::size_t row = index / columns;
        const std::size_t column = index % columns;
        // each unit gets a slot offset from the requested formation center
        const glm::vec3 offset(
            start_x + static_cast<float>(column) * spacing,
            0.0f,
            start_z + static_cast<float>(row) * spacing);
        const UnitState& unit = *ordered_units[index];
        any_accepted |= issueOrder(unit.unit_id, RtsOrder{
            order_type,
            center + offset,
            glm::vec3(0.0f),
            target_unit_id,
            unit.move_speed,
            unit.radius + 0.08f,
            0,
            target_building_id
        }, queue_when_busy);
    }
    return any_accepted;
}

bool RtsWorld::clearOrders(std::uint32_t unit_id) {
    UnitState* unit = findUnit(unit_id);
    if (!unit) {
        return false;
    }

    unit->holding_position = false;
    unit->order_queue.clear();
    unit->active_order = std::nullopt;
    clearUnitMotion(*unit);
    return true;
}

void RtsWorld::update(float delta_seconds) {
    if (delta_seconds <= 0.0f) {
        return;
    }

    // one simulation tick roughly does
    // clear last frame events and refresh fog
    // decay unit timers
    // resolve projectile and tower combat
    // ask ai for commands
    // advance production queues
    // run each units order state machine
    // cleanup dead entities and then check win state
    combat_events_.clear();
    updateFogOfWar();
    for (auto& entry : units_) {
        UnitState& unit = entry.second;
        // cooldown timers and hit flash timers decay independently of what order the unit is running
        unit.attack_cooldown_remaining =
            std::max(0.0f, unit.attack_cooldown_remaining - delta_seconds);
        unit.harvest_cooldown_remaining =
            std::max(0.0f, unit.harvest_cooldown_remaining - delta_seconds);
        unit.recent_hit_timer =
            std::max(0.0f, unit.recent_hit_timer - delta_seconds);
    }

    // projectile logic resolves through callbacks into the current world state
    // combat itself stays decoupled and asks the world to translate ids into live targets
    combat_.updateProjectiles(
        delta_seconds,
        [this](const RtsCombatSystem::ProjectileState& projectile)
            -> std::optional<RtsCombatSystem::TargetSnapshot> {
            if (projectile.target_unit_id != 0) {
                const UnitState* target = findUnit(projectile.target_unit_id);
                if (!target) {
                    return std::nullopt;
                }
                return RtsCombatSystem::TargetSnapshot{
                    target->unit_id,
                    target->team,
                    target->position,
                    target->radius,
                    target->health > 0.0f,
                    false
                };
            }

            const OwnedBuildingState* target = findOwnedBuilding(projectile.target_building_id);
            const BuildingInstance* building = buildings_.findBuilding(projectile.target_building_id);
            if (!target || !building) {
                return std::nullopt;
            }
            return RtsCombatSystem::TargetSnapshot{
                target->building_id,
                target->team,
                buildingCenter(*building),
                buildingInteractionRadius(*building),
                target->health > 0.0f,
                true
            };
        },
        [this](const RtsCombatSystem::ProjectileState& projectile,
               const RtsCombatSystem::TargetSnapshot& target,
               std::vector<RtsEvent>& events) {
            if (!target.is_building) {
                // unit impacts directly reduce the targets current health
                UnitState* resolved_target = findUnit(target.target_id);
                if (!resolved_target) {
                    return;
                }
                const float previous_health = resolved_target->health;
                resolved_target->health = std::max(0.0f, resolved_target->health - projectile.damage);
                resolved_target->recent_hit_timer = kRecentHitDuration;
                events.push_back(RtsEvent{
                    RtsEventType::projectile_hit,
                    projectile.source_id,
                    projectile.target_unit_id,
                    projectile.projectile_id,
                    projectile.source_building_id,
                    projectile.team,
                    -1,
                    resolved_target->position,
                    projectile.from_tower
                });
                if (previous_health > 0.0f && resolved_target->health <= 0.0f) {
                    events.push_back(RtsEvent{
                        RtsEventType::unit_died,
                        projectile.source_id,
                        projectile.target_unit_id,
                        projectile.projectile_id,
                        projectile.source_building_id,
                        projectile.team,
                        -1,
                        resolved_target->position,
                        projectile.from_tower
                    });
                }
                return;
            }

            // building impacts also emit building specific damage and destroy events
            OwnedBuildingState* resolved_target = findOwnedBuilding(target.target_id);
            const BuildingInstance* building = buildings_.findBuilding(target.target_id);
            if (!resolved_target || !building) {
                return;
            }
            const float previous_health = resolved_target->health;
            resolved_target->health = std::max(0.0f, resolved_target->health - projectile.damage);
            const glm::vec3 center = buildingCenter(*building);
            events.push_back(RtsEvent{
                RtsEventType::projectile_hit,
                projectile.source_id,
                target.target_id,
                projectile.projectile_id,
                projectile.source_building_id,
                projectile.team,
                -1,
                center,
                projectile.from_tower
            });
            events.push_back(RtsEvent{
                RtsEventType::building_damaged,
                projectile.source_id,
                target.target_id,
                projectile.projectile_id,
                target.target_id,
                projectile.team,
                -1,
                center,
                projectile.from_tower
            });
            if (previous_health > 0.0f && resolved_target->health <= 0.0f) {
                events.push_back(RtsEvent{
                    RtsEventType::building_destroyed,
                    projectile.source_id,
                    target.target_id,
                    projectile.projectile_id,
                    target.target_id,
                    projectile.team,
                    -1,
                    center,
                    projectile.from_tower
                });
            }
        },
        combat_events_);
    cleanupDestroyedEntities();
    // after projectile cleanup towers can select fresh targets from the surviving world state
    combat_.updateTowers(
        delta_seconds,
        [this](std::uint32_t building_id) -> std::optional<glm::vec3> {
            const BuildingInstance* building = buildings_.findBuilding(building_id);
            return building ? std::optional<glm::vec3>(buildingCenter(*building)) : std::nullopt;
        },
        [this](int team, const glm::vec3& center, float radius)
            -> std::optional<RtsCombatSystem::TargetSnapshot> {
            const std::optional<std::uint32_t> enemy_id = findNearestEnemyUnit(team, center, radius);
            if (enemy_id.has_value()) {
                const UnitState* enemy = findUnit(enemy_id.value());
                if (enemy) {
                    return RtsCombatSystem::TargetSnapshot{
                        enemy->unit_id,
                        enemy->team,
                        enemy->position,
                        enemy->radius,
                        enemy->health > 0.0f,
                        false
                    };
                }
            }

            const std::optional<std::uint32_t> building_id =
                findNearestEnemyBuilding(team, center, radius);
            if (!building_id.has_value()) {
                return std::nullopt;
            }
            const OwnedBuildingState* building = findOwnedBuilding(building_id.value());
            const BuildingInstance* building_instance = buildings_.findBuilding(building_id.value());
            if (!building || !building_instance) {
                return std::nullopt;
            }
            return RtsCombatSystem::TargetSnapshot{
                building->building_id,
                building->team,
                buildingCenter(*building_instance),
                buildingInteractionRadius(*building_instance),
                building->health > 0.0f,
                true
            };
        },
        combat_events_);
    // ai works from snapshots so it cannot directly mutate the world while deciding
    // once it returns commands we replay them through the same helpers the player code uses
    const std::vector<RtsAiCommand> ai_commands = ai_.update(
        delta_seconds,
        RtsAiFrame{
            unitSnapshots(),
            buildingSnapshots(),
            productionSnapshots(),
            resourceNodeSnapshots()
        });
    for (const RtsAiCommand& command : ai_commands) {
        // translate abstract ai commands back into the same public command helpers used elsewhere
        switch (command.type) {
        case RtsAiCommandType::issue_harvest:
            if (command.unit_id != 0 && command.resource_node_id != 0) {
                issueHarvestOrder(command.unit_id, command.resource_node_id);
            }
            break;
        case RtsAiCommandType::issue_move:
            if (!command.unit_ids.empty()) {
                issueFormationOrder(command.unit_ids,
                                    command.target_position,
                                    RtsOrderType::move,
                                    1.2f);
            }
            break;
        case RtsAiCommandType::issue_attack_move:
            if (!command.unit_ids.empty()) {
                issueFormationOrder(command.unit_ids,
                                    command.target_position,
                                    RtsOrderType::attack_move,
                                    1.2f,
                                    false,
                                    command.target_unit_id);
            }
            break;
        case RtsAiCommandType::enqueue_production:
            if (command.building_id != 0 && !command.archetype_id.empty()) {
                enqueueProduction(command.building_id, command.archetype_id);
            }
            break;
        }
    }
    // production advances after ai so newly queued entries start counting down right away
    production_.update(
        delta_seconds,
        [this](std::uint32_t building_id) {
            const OwnedBuildingState* building = findOwnedBuilding(building_id);
            return building && !building->under_construction && buildings_.findBuilding(building_id) != nullptr;
        },
        [this](std::uint32_t building_id, const std::string& unit_archetype_id, const glm::vec3& rally_point) {
            return spawnProducedUnit(building_id, unit_archetype_id, rally_point);
        });

    for (auto& entry : units_) {
        UnitState& unit = entry.second;
        if (unit.health <= 0.0f) {
            continue;
        }

        if (unit.holding_position) {
            // hold position units never path but still take free shots at nearby enemies
            clearUnitMotion(unit);
            const std::optional<std::uint32_t> enemy_id =
                findNearestEnemyUnit(unit.team, unit.position, unit.attack_range + unit.radius + 0.1f);
            if (enemy_id.has_value()) {
                const UnitState* enemy = findUnit(enemy_id.value());
                if (enemy) {
                    tryAttackUnit(unit, *enemy);
                }
            } else {
                const std::optional<std::uint32_t> building_id =
                    findNearestEnemyBuilding(unit.team,
                                             unit.position,
                                             unit.attack_range + unit.radius + 0.1f);
                if (building_id.has_value()) {
                    const OwnedBuildingState* building = findOwnedBuilding(building_id.value());
                    if (building) {
                        tryAttackBuilding(unit, *building);
                    }
                }
            }
            continue;
        }

        pullNextQueuedOrder(unit);
        // queued orders only become visible to the state machine once this pull happens
        if (!unit.active_order.has_value()) {
            // idle units should not keep stale path state around
            clearUnitMotion(unit);
            continue;
        }

        // active_order behaves like a small per unit state machine
        // each branch decides whether the order finished stalled or should keep running next frame
        updateActiveOrder(unit, delta_seconds);
    }

    cleanupDestroyedEntities();
    emitMatchEndedEventIfNeeded();
}

bool RtsWorld::isCellTraversable(const GridCoord& cell) const {
    // one helper combines terrain walkability with building occupancy blocking
    return terrain_.isValidCell(cell) &&
           terrain_.isWalkable(cell) &&
           !buildings_.blocksMovement(cell);
}

bool RtsWorld::findNearestTraversableCell(const GridCoord& start_cell, GridCoord& out_cell) const {
    // small outward search used when the requested goal cell is blocked
    for (int radius = 0; radius <= 4; ++radius) {
        for (int y = -radius; y <= radius; ++y) {
            for (int x = -radius; x <= radius; ++x) {
                const GridCoord candidate{start_cell.x + x, start_cell.y + y};
                if (!isCellTraversable(candidate)) {
                    continue;
                }
                out_cell = candidate;
                return true;
            }
        }
    }
    return false;
}

void RtsWorld::clearUnitMotion(UnitState& unit) {
    // clears only path following state not health cooldowns or queued orders
    unit.path_points.clear();
    unit.path_index = 0;
    unit.has_goal_cell = false;
    unit.moving = false;
}

bool RtsWorld::assignPath(UnitState& unit,
                          const glm::vec3& target_position,
                          float arrival_radius) {
    GridCoord start_cell{};
    if (!terrain_.worldToCell(unit.position, start_cell)) {
        return false;
    }

    GridCoord goal_cell{};
    const bool target_cell_valid = terrain_.worldToCell(target_position, goal_cell);
    if (!target_cell_valid) {
        return false;
    }

    // if the requested goal cell is blocked search for a nearby reachable substitute
    GridCoord resolved_goal = goal_cell;
    if (!isCellTraversable(resolved_goal) &&
        !findNearestTraversableCell(goal_cell, resolved_goal)) {
        return false;
    }

    if (start_cell.x == resolved_goal.x &&
        start_cell.y == resolved_goal.y &&
        planar_distance(unit.position, target_position) <= arrival_radius) {
        unit.position = snapped_target_position(
            terrain_, resolved_goal, target_position, target_cell_valid, goal_cell);
        clearUnitMotion(unit);
        return true;
    }

    const std::vector<glm::vec3> world_path =
        pathfinder_.findWorldPath(terrain_, buildings_, start_cell, resolved_goal);
    if (world_path.empty()) {
        return false;
    }

    // skip the first point because it is the units current cell center
    // movement only needs future waypoints beyond where the unit already stands
    unit.path_points.clear();
    unit.path_index = 0;
    for (std::size_t i = 1; i < world_path.size(); ++i) {
        unit.path_points.push_back(world_path[i]);
    }

    // append a final exact endpoint when the cell center alone is not close enough
    const glm::vec3 resolved_target = snapped_target_position(
        terrain_, resolved_goal, target_position, target_cell_valid, goal_cell);
    if (unit.path_points.empty() ||
        planar_distance(unit.path_points.back(), resolved_target) > arrival_radius) {
        unit.path_points.push_back(resolved_target);
    }

    unit.goal_cell = resolved_goal;
    unit.has_goal_cell = true;
    unit.moving = !unit.path_points.empty();
    return true;
}

bool RtsWorld::moveUnitToward(UnitState& unit,
                              const glm::vec3& target_position,
                              float move_speed,
                              float arrival_radius,
                              float delta_seconds) {
    if (planar_distance(unit.position, target_position) <= arrival_radius) {
        // close enough means finish immediately and snap y back to terrain elevation
        GridCoord cell{};
        if (terrain_.worldToCell(unit.position, cell)) {
            unit.position.y = terrain_.elevation(cell);
        }
        clearUnitMotion(unit);
        return true;
    }

    GridCoord desired_goal{};
    if (!terrain_.worldToCell(target_position, desired_goal)) {
        clearUnitMotion(unit);
        return false;
    }

    // rebuild whenever the desired goal cell changed or the existing path is exhausted
    // repeated frames toward the same goal can therefore keep reusing the cached path
    if (!unit.has_goal_cell ||
        desired_goal.x != unit.goal_cell.x ||
        desired_goal.y != unit.goal_cell.y ||
        unit.path_index >= unit.path_points.size()) {
        if (!assignPath(unit, target_position, arrival_radius)) {
            clearUnitMotion(unit);
            return false;
        }
    }

    while (unit.path_index < unit.path_points.size()) {
        const glm::vec3 waypoint = unit.path_points[unit.path_index];
        const glm::vec3 to_waypoint = waypoint - unit.position;
        const float distance_to_waypoint = glm::length(to_waypoint);
        if (distance_to_waypoint <= arrival_radius) {
            // consume already reached waypoints in the same frame before spending movement on the next one
            unit.position = waypoint;
            ++unit.path_index;
            continue;
        }

        GridCoord current_cell{};
        if (!terrain_.worldToCell(unit.position, current_cell)) {
            clearUnitMotion(unit);
            return false;
        }

        const float terrain_cost = std::max(terrain_.movementCost(current_cell), 0.5f);
        // terrain cost slows the effective distance the unit can move this frame
        // roads feel faster because their cost is smaller while forests and rough ground feel slower
        const float max_step = (move_speed / terrain_cost) * delta_seconds;
        if (max_step >= distance_to_waypoint) {
            // if we can fully reach the waypoint this frame snap exactly onto it
            unit.position = waypoint;
            ++unit.path_index;
        } else {
            // otherwise advance partway toward the waypoint along the normalized direction
            const glm::vec3 direction = to_waypoint / distance_to_waypoint;
            unit.position += direction * max_step;
        }

        GridCoord resolved_cell{};
        if (terrain_.worldToCell(unit.position, resolved_cell)) {
            unit.position.y = terrain_.elevation(resolved_cell);
        }
        unit.moving = true;
        return false;
    }

    GridCoord final_cell{};
    if (terrain_.worldToCell(unit.position, final_cell)) {
        unit.position.y = terrain_.elevation(final_cell);
    }
    clearUnitMotion(unit);
    return true;
}

bool RtsWorld::tryAttackUnit(UnitState& attacker, const UnitState& target) {
    const float effective_range = attacker.attack_range + target.radius;
    if (planar_distance(attacker.position, target.position) > effective_range) {
        return false;
    }
    if (attacker.attack_cooldown_remaining > 0.0f || target.health <= 0.0f) {
        return true;
    }

    // attacking means spawning a projectile and starting cooldown not applying instant damage here
    // health is actually removed later when projectile impact is resolved
    if (!combat_.spawnProjectile(attacker.unit_id,
                                 target.unit_id,
                                 0,
                                 0,
                                 attacker.team,
                                 attacker.position,
                                 attacker.projectile_speed,
                                 attacker.attack_damage,
                                 false,
                                 combat_events_)) {
        return false;
    }
    attacker.attack_cooldown_remaining = attacker.attack_cooldown;
    return true;
}

bool RtsWorld::tryAttackBuilding(UnitState& attacker, const OwnedBuildingState& target) {
    const BuildingInstance* building = buildings_.findBuilding(target.building_id);
    if (!building) {
        return false;
    }

    const glm::vec3 center = buildingCenter(*building);
    const float effective_range = attacker.attack_range + buildingInteractionRadius(*building);
    if (planar_distance(attacker.position, center) > effective_range) {
        return false;
    }
    if (attacker.attack_cooldown_remaining > 0.0f || target.health <= 0.0f) {
        // returning true here means we are already in a valid attacking posture even if the shot is cooling down
        return true;
    }

    if (!combat_.spawnProjectile(attacker.unit_id,
                                 0,
                                 target.building_id,
                                 0,
                                 attacker.team,
                                 attacker.position,
                                 attacker.projectile_speed,
                                 attacker.attack_damage,
                                 false,
                                 combat_events_)) {
        return false;
    }
    attacker.attack_cooldown_remaining = attacker.attack_cooldown;
    return true;
}

std::optional<std::uint32_t> RtsWorld::findNearestEnemyUnit(int team,
                                                            const glm::vec3& center,
                                                            float radius) const {
    std::optional<std::uint32_t> nearest_id = std::nullopt;
    float nearest_distance = std::numeric_limits<float>::max();
    for (const auto& entry : units_) {
        const UnitState& candidate = entry.second;
        if (candidate.team == team || candidate.health <= 0.0f) {
            continue;
        }

        // nearest searches are intentionally simple and stop at distance based best target
        const float distance = planar_distance(candidate.position, center);
        if (distance > radius || distance >= nearest_distance) {
            continue;
        }
        nearest_distance = distance;
        nearest_id = candidate.unit_id;
    }
    return nearest_id;
}

std::optional<std::uint32_t> RtsWorld::findNearestEnemyBuilding(int team,
                                                                const glm::vec3& center,
                                                                float radius) const {
    std::optional<std::uint32_t> nearest_id = std::nullopt;
    float nearest_distance = std::numeric_limits<float>::max();
    for (const auto& entry : owned_buildings_) {
        const OwnedBuildingState& candidate = entry.second;
        if (candidate.team == team || candidate.health <= 0.0f) {
            continue;
        }

        const BuildingInstance* building = buildings_.findBuilding(candidate.building_id);
        if (!building) {
            continue;
        }

        const float distance = planar_distance(buildingCenter(*building), center);
        if (distance > radius || distance >= nearest_distance) {
            continue;
        }
        nearest_distance = distance;
        nearest_id = candidate.building_id;
    }
    return nearest_id;
}

std::optional<std::uint32_t> RtsWorld::findNearestFriendlyDropoffBuilding(int team,
                                                                          const glm::vec3& center) const {
    std::optional<std::uint32_t> nearest_id = std::nullopt;
    float nearest_distance = std::numeric_limits<float>::max();
    for (const auto& entry : owned_buildings_) {
        const OwnedBuildingState& building = entry.second;
        if (building.team != team) {
            continue;
        }
        if (building.under_construction) {
            continue;
        }
        // only completed buildings that explicitly accept dropoff count here
        const BuildingInstance* building_instance = buildings_.findBuilding(building.building_id);
        const RtsBuildingArchetype* building_archetype = findBuildingArchetype(building.archetype_id);
        if (!building_instance || !building_archetype || !building_archetype->accepts_resource_dropoff) {
            continue;
        }

        const glm::vec3 building_position = buildingCenter(*building_instance);
        const float distance = planar_distance(center, building_position);
        if (distance < nearest_distance) {
            nearest_distance = distance;
            nearest_id = building.building_id;
        }
    }
    return nearest_id;
}

bool RtsWorld::handleHarvestOrder(UnitState& unit, RtsOrder& order, float delta_seconds) {
    const RtsUnitArchetype* archetype = findUnitArchetype(unit.archetype_id);
    if (!archetype || !archetype->can_harvest) {
        unit.active_order = std::nullopt;
        clearUnitMotion(unit);
        return false;
    }

    if (unit.carried_resource_amount >= archetype->carry_capacity &&
        !unit.carried_resource_id.empty()) {
        // once full the worker temporarily switches from harvesting to deposit behavior
        // this is still the same harvest order not a separate queued return order
        const std::optional<std::uint32_t> dropoff_id =
            findNearestFriendlyDropoffBuilding(unit.team, unit.position);
        if (!dropoff_id.has_value()) {
            // if there is nowhere to deposit the worker just stalls instead of deleting the order
            clearUnitMotion(unit);
            return false;
        }

        const BuildingInstance* building = buildings_.findBuilding(dropoff_id.value());
        if (!building) {
            clearUnitMotion(unit);
            return false;
        }
        const glm::vec3 dropoff_position = buildingCenter(*building);
        if (!moveUnitToward(unit, dropoff_position, unit.move_speed, unit.radius + 0.1f, delta_seconds)) {
            return false;
        }

        // after arriving deposit the carried payload into the team stockpile
        addTeamResourceAmount(unit.team, unit.carried_resource_id, unit.carried_resource_amount);
        combat_events_.push_back(RtsEvent{
            RtsEventType::resources_deposited,
            unit.unit_id,
            0,
            0,
            dropoff_id.value(),
            unit.team,
            -1,
            dropoff_position,
            false,
            unit.carried_resource_id,
            unit.carried_resource_amount
        });
        unit.carried_resource_amount = 0;
        unit.carried_resource_id.clear();
        // leaving the order active makes the worker loop back to the node automatically
        return false;
    }

    const RtsEconomySystem::ResourceNodeState* node = economy_.findResourceNode(order.target_resource_node_id);
    if (!node) {
        // resource node vanished or was emptied by someone else
        unit.active_order = std::nullopt;
        clearUnitMotion(unit);
        return false;
    }

    // otherwise move to the resource node and harvest when in range
    // harvest therefore alternates travel gather travel deposit until canceled or the node disappears
    const glm::vec3 node_position = terrain_.cellCenter(node->cell);
    if (!moveUnitToward(unit, node_position, unit.move_speed, unit.radius + 0.15f, delta_seconds)) {
        return false;
    }

    if (unit.harvest_cooldown_remaining > 0.0f) {
        // stay on the order while waiting for the next harvest tick
        return false;
    }

    const int remaining_capacity = archetype->carry_capacity - unit.carried_resource_amount;
    const int requested_amount = std::min(archetype->harvest_amount, remaining_capacity);
    // the economy system may return less than requested if the node is nearly depleted
    const int harvested_amount = economy_.harvestResourceNode(order.target_resource_node_id, requested_amount);
    if (harvested_amount <= 0) {
        unit.active_order = std::nullopt;
        clearUnitMotion(unit);
        return false;
    }

    unit.carried_resource_id = node->resource_id;
    unit.carried_resource_amount += harvested_amount;
    unit.harvest_cooldown_remaining = unit.harvest_cooldown;
    combat_events_.push_back(RtsEvent{
        RtsEventType::resource_harvested,
        unit.unit_id,
        order.target_resource_node_id,
        0,
        0,
        unit.team,
        -1,
        node_position,
        false,
        node->resource_id,
        harvested_amount
    });
    return false;
}

bool RtsWorld::handleConstructOrder(UnitState& unit, RtsOrder& order, float delta_seconds) {
    OwnedBuildingState* building = findOwnedBuilding(order.target_building_id);
    const BuildingInstance* building_instance = buildings_.findBuilding(order.target_building_id);
    if (!building || !building_instance || !building->under_construction) {
        // if the construction target disappeared cancel the order
        unit.active_order = std::nullopt;
        clearUnitMotion(unit);
        return false;
    }

    // construction progresses only while the builder stays inside interaction range
    // that makes the worker physically commit to the job instead of building from anywhere
    const glm::vec3 center = buildingCenter(*building_instance);
    const float interaction_radius =
        std::max(order.arrival_radius, unit.radius + buildingInteractionRadius(*building_instance));
    if (!moveUnitToward(unit, center, unit.move_speed, interaction_radius, delta_seconds)) {
        return false;
    }

    if (building->build_time <= 0.0f) {
        // zero build time means instant finish once the builder reaches the site
        finishBuildingConstruction(*building);
        unit.active_order = std::nullopt;
        clearUnitMotion(unit);
        return true;
    }

    // build progress is normalized so all buildings share the same zero to one completion rule
    building->build_progress =
        std::min(1.0f, building->build_progress + delta_seconds / building->build_time);
    // health rises with construction progress so unfinished buildings are still damageable
    building->health =
        std::max(kMinBuildingHealth, building->max_health * std::max(building->build_progress, 0.1f));
    if (building->build_progress >= 1.0f) {
        finishBuildingConstruction(*building);
        unit.active_order = std::nullopt;
        clearUnitMotion(unit);
        return true;
    }

    // once at the site the worker mostly stands in place while the timer advances
    clearUnitMotion(unit);
    return false;
}

bool RtsWorld::handleRepairOrder(UnitState& unit, RtsOrder& order, float delta_seconds) {
    OwnedBuildingState* building = findOwnedBuilding(order.target_building_id);
    const BuildingInstance* building_instance = buildings_.findBuilding(order.target_building_id);
    if (!building || !building_instance) {
        // invalid repair target cancels the order
        unit.active_order = std::nullopt;
        clearUnitMotion(unit);
        return false;
    }
    if (building->under_construction) {
        // unfinished buildings reuse the construction logic instead of separate repair logic
        return handleConstructOrder(unit, order, delta_seconds);
    }
    if (building->health >= building->max_health) {
        // fully repaired buildings release the unit back to idle or queued work
        unit.active_order = std::nullopt;
        clearUnitMotion(unit);
        return true;
    }

    const glm::vec3 center = buildingCenter(*building_instance);
    const float interaction_radius =
        std::max(order.arrival_radius, unit.radius + buildingInteractionRadius(*building_instance));
    if (!moveUnitToward(unit, center, unit.move_speed, interaction_radius, delta_seconds)) {
        return false;
    }

    // repair is continuous and may emit repeated building_repaired events across many frames
    // unlike construction it only restores health and never changes completion state
    const float previous_health = building->health;
    building->health = std::min(building->max_health,
                                building->health + building->repair_rate * delta_seconds);
    if (building->health > previous_health) {
        combat_events_.push_back(RtsEvent{
            RtsEventType::building_repaired,
            unit.unit_id,
            building->building_id,
            0,
            building->building_id,
            unit.team,
            -1,
            center,
            false
        });
    }
    if (building->health >= building->max_health) {
        unit.active_order = std::nullopt;
    }
    clearUnitMotion(unit);
    return true;
}

void RtsWorld::cleanupDestroyedEntities() {
    // remove dead units first because they have no occupancy footprint to clear
    std::vector<std::uint32_t> dead_units{};
    for (const auto& entry : units_) {
        if (entry.second.health <= 0.0f) {
            dead_units.push_back(entry.first);
        }
    }
    for (const std::uint32_t unit_id : dead_units) {
        units_.erase(unit_id);
    }

    // destroyed buildings go through removeBuilding so occupancy and tower state get updated too
    std::vector<std::uint32_t> destroyed_buildings{};
    for (const auto& entry : owned_buildings_) {
        if (entry.second.health <= 0.0f && buildings_.findBuilding(entry.first)) {
            destroyed_buildings.push_back(entry.first);
        }
    }
    for (const std::uint32_t building_id : destroyed_buildings) {
        removeBuilding(building_id);
    }

    // finally scrub orphaned owned building metadata left behind by any earlier removals
    std::vector<std::uint32_t> invalid_owned_buildings{};
    for (const auto& entry : owned_buildings_) {
        if (!buildings_.findBuilding(entry.first)) {
            invalid_owned_buildings.push_back(entry.first);
        }
    }
    for (const std::uint32_t building_id : invalid_owned_buildings) {
        owned_buildings_.erase(building_id);
        production_.unregisterBuilding(building_id);
        combat_.removeTower(building_id);
    }
}

glm::vec3 RtsWorld::buildingCenter(const BuildingInstance& building) const {
    // use the average of all covered cell centers so large footprints get a sensible center point
    const std::vector<GridCoord> cells =
        terrain_.cellsInFootprint(building.anchor,
                                  building.footprint_width,
                                  building.footprint_height);
    if (cells.empty()) {
        return glm::vec3(0.0f);
    }

    glm::vec3 sum(0.0f);
    for (const GridCoord& cell : cells) {
        sum += terrain_.cellCenter(cell);
    }
    return sum / static_cast<float>(cells.size());
}

float RtsWorld::buildingInteractionRadius(const BuildingInstance& building) const {
    // approximate reach with half the footprint diagonal in world units
    return 0.5f * terrain_.cellSize() *
           std::sqrt(static_cast<float>(building.footprint_width * building.footprint_width +
                                        building.footprint_height * building.footprint_height));
}

void RtsWorld::pullNextQueuedOrder(UnitState& unit) {
    if (unit.active_order.has_value() || unit.order_queue.empty()) {
        return;
    }

    // queued orders only start once the active slot becomes free
    // units never execute two independent orders at the same time
    unit.active_order = unit.order_queue.front();
    unit.order_queue.pop_front();
    clearUnitMotion(unit);
}

void RtsWorld::updateActiveOrder(UnitState& unit, float delta_seconds) {
    if (!unit.active_order.has_value()) {
        clearUnitMotion(unit);
        return;
    }

    RtsOrder& order = unit.active_order.value();
    const float move_speed =
        order.move_speed > 0.0f ? order.move_speed : unit.move_speed;
    const float arrival_radius = std::max(order.arrival_radius, kMinArrivalRadius);

    // helper lambdas keep the switch cases focused on order logic rather than attack details
    // multiple order types reuse the same chase until in range then stop and fire pattern
    auto engage_enemy = [&](const UnitState& enemy) {
        // tryAttackUnit returns false only when we are out of range or could not spawn the projectile
        if (!tryAttackUnit(unit, enemy)) {
            moveUnitToward(unit, enemy.position, move_speed, unit.attack_range, delta_seconds);
        } else {
            // once in valid attack posture stop moving so we do not overshoot through the target
            clearUnitMotion(unit);
        }
    };
    auto engage_building = [&](const OwnedBuildingState& building) {
        const BuildingInstance* building_instance = buildings_.findBuilding(building.building_id);
        if (!building_instance) {
            return;
        }
        const glm::vec3 center = buildingCenter(*building_instance);
        if (!tryAttackBuilding(unit, building)) {
            // for buildings use a larger arrival threshold based on the footprint radius
            moveUnitToward(unit,
                           center,
                           move_speed,
                           std::max(unit.attack_range,
                                    unit.attack_range + buildingInteractionRadius(*building_instance)),
                           delta_seconds);
        } else {
            clearUnitMotion(unit);
        }
    };

    switch (order.type) {
    case RtsOrderType::move:
        // pure move finishes and clears itself on arrival
        if (moveUnitToward(unit, order.target_position, move_speed, arrival_radius, delta_seconds)) {
            unit.active_order = std::nullopt;
        }
        return;

    case RtsOrderType::attack_move: {
        // attack move tries explicit focus targets first then nearby aggro targets then resumes marching
        // this makes explicit enemy clicks feel sticky while still allowing general self defense
        if (order.target_unit_id != 0) {
            const UnitState* focus_target = findUnit(order.target_unit_id);
            if (focus_target && focus_target->team != unit.team && focus_target->health > 0.0f) {
                // explicit focus target wins over opportunistic aggro while it is still valid
                engage_enemy(*focus_target);
                return;
            }
            // clear stale focus targets once they die or become invalid
            order.target_unit_id = 0;
        }
        if (order.target_building_id != 0) {
            const OwnedBuildingState* focus_target = findOwnedBuilding(order.target_building_id);
            if (focus_target && focus_target->team != unit.team && focus_target->health > 0.0f) {
                engage_building(*focus_target);
                return;
            }
            order.target_building_id = 0;
        }

        // once there is no explicit focus target fall back to nearby aggro targets
        // after combat interrupts it the unit resumes marching toward the original destination
        const std::optional<std::uint32_t> enemy_id =
            findNearestEnemyUnit(unit.team, unit.position, unit.aggro_range);
        if (enemy_id.has_value()) {
            const UnitState* enemy = findUnit(enemy_id.value());
            if (enemy) {
                engage_enemy(*enemy);
                return;
            }
        }
        const std::optional<std::uint32_t> building_id =
            findNearestEnemyBuilding(unit.team, unit.position, unit.aggro_range);
        if (building_id.has_value()) {
            const OwnedBuildingState* building = findOwnedBuilding(building_id.value());
            if (building) {
                engage_building(*building);
                return;
            }
        }

        if (moveUnitToward(unit, order.target_position, move_speed, arrival_radius, delta_seconds)) {
            // when the final positional target is reached the attack move order completes
            unit.active_order = std::nullopt;
        }
        return;
    }

    case RtsOrderType::patrol: {
        // patrol bounces between two endpoints while still opportunistically fighting enemies
        const std::optional<std::uint32_t> enemy_id =
            findNearestEnemyUnit(unit.team, unit.position, unit.aggro_range);
        if (enemy_id.has_value()) {
            const UnitState* enemy = findUnit(enemy_id.value());
            if (enemy) {
                engage_enemy(*enemy);
                return;
            }
        }
        const std::optional<std::uint32_t> building_id =
            findNearestEnemyBuilding(unit.team, unit.position, unit.aggro_range);
        if (building_id.has_value()) {
            const OwnedBuildingState* building = findOwnedBuilding(building_id.value());
            if (building) {
                engage_building(*building);
                return;
            }
        }

        if (moveUnitToward(unit, order.target_position, move_speed, arrival_radius, delta_seconds)) {
            // swap the patrol endpoints so the next update starts heading back the other way
            std::swap(order.target_position, order.secondary_target_position);
        }
        return;
    }

    case RtsOrderType::guard: {
        // guard follows a friendly unit loosely and engages threats around that friendly
        // the guarded unit acts like the center of the defenders awareness bubble
        const UnitState* guarded_unit = findUnit(order.target_unit_id);
        if (!guarded_unit || guarded_unit->team != unit.team) {
            unit.active_order = std::nullopt;
            clearUnitMotion(unit);
            return;
        }

        const std::optional<std::uint32_t> enemy_id =
            findNearestEnemyUnit(unit.team, guarded_unit->position, unit.aggro_range);
        if (enemy_id.has_value()) {
            const UnitState* enemy = findUnit(enemy_id.value());
            if (enemy) {
                engage_enemy(*enemy);
                return;
            }
        }
        const std::optional<std::uint32_t> building_id =
            findNearestEnemyBuilding(unit.team, guarded_unit->position, unit.aggro_range);
        if (building_id.has_value()) {
            const OwnedBuildingState* building = findOwnedBuilding(building_id.value());
            if (building) {
                engage_building(*building);
                return;
            }
        }

        if (planar_distance(unit.position, guarded_unit->position) > unit.guard_radius) {
            // only move if we drifted outside the loose guard ring
            moveUnitToward(unit,
                           guarded_unit->position,
                           move_speed,
                           std::max(unit.guard_radius * 0.5f, arrival_radius),
                           delta_seconds);
            return;
        }

        clearUnitMotion(unit);
        return;
    }

    case RtsOrderType::harvest:
        handleHarvestOrder(unit, order, delta_seconds);
        return;

    case RtsOrderType::construct:
        handleConstructOrder(unit, order, delta_seconds);
        return;

    case RtsOrderType::repair:
        handleRepairOrder(unit, order, delta_seconds);
        return;

    case RtsOrderType::stop:
        unit.holding_position = false;
        unit.active_order = std::nullopt;
        unit.order_queue.clear();
        clearUnitMotion(unit);
        return;

    case RtsOrderType::hold_position:
        unit.holding_position = true;
        unit.active_order = std::nullopt;
        unit.order_queue.clear();
        clearUnitMotion(unit);
        return;
    }
}

bool RtsWorld::buildingCanProduceUnit(const OwnedBuildingState& building,
                                      const std::string& unit_archetype_id) const {
    const RtsBuildingArchetype* building_archetype = findBuildingArchetype(building.archetype_id);
    if (!building_archetype) {
        return false;
    }
    // production permission is data driven by the buildings producible_unit_archetypes list
    return std::find(building_archetype->producible_unit_archetypes.begin(),
                     building_archetype->producible_unit_archetypes.end(),
                     unit_archetype_id) != building_archetype->producible_unit_archetypes.end();
}

bool RtsWorld::spawnProducedUnit(std::uint32_t building_id,
                                 const std::string& unit_archetype_id,
                                 const glm::vec3& rally_point) {
    const auto owned_it = owned_buildings_.find(building_id);
    if (owned_it == owned_buildings_.end()) {
        return false;
    }
    const OwnedBuildingState& building = owned_it->second;
    const BuildingInstance* building_instance = buildings_.findBuilding(building_id);
    if (!building_instance) {
        return false;
    }

    // spawn near the building center then nudge to the nearest traversable cell
    // this avoids producing units inside blocked building footprint cells
    GridCoord spawn_origin_cell{};
    glm::vec3 spawn_origin = buildingCenter(*building_instance);
    if (!terrain_.worldToCell(spawn_origin, spawn_origin_cell)) {
        return false;
    }

    GridCoord rally_cell{};
    glm::vec3 spawn_target = rally_point;
    if (!terrain_.worldToCell(spawn_target, rally_cell)) {
        // if the rally point is off map fall back to the building center
        spawn_target = spawn_origin;
        if (!terrain_.worldToCell(spawn_target, rally_cell)) {
            return false;
        }
    }

    GridCoord spawn_cell{};
    if (!findNearestTraversableCell(spawn_origin_cell, spawn_cell)) {
        return false;
    }

    const std::uint32_t unit_id = reserveNextUnitId();
    const glm::vec3 spawn_position = terrain_.cellCenter(spawn_cell);
    if (!addUnitFromArchetype(unit_id, building.team, spawn_position, unit_archetype_id)) {
        return false;
    }

    if (planar_distance(spawn_position, spawn_target) > 0.1f) {
        // produced units immediately inherit the buildings rally point as a move order
        // rally points are just ordinary move orders so they reuse normal pathing and arrival logic
        issueOrder(unit_id, RtsOrder{
            RtsOrderType::move,
            spawn_target,
            glm::vec3(0.0f),
            0,
            0.0f,
            0.1f
        });
    }
    combat_events_.push_back(RtsEvent{
        RtsEventType::production_completed,
        unit_id,
        0,
        0,
        building_id,
        building.team,
        -1,
        spawn_position,
        false,
        {},
        0,
        unit_archetype_id
    });
    combat_events_.push_back(RtsEvent{
        RtsEventType::unit_spawned,
        unit_id,
        0,
        0,
        building_id,
        building.team,
        -1,
        spawn_position,
        false,
        {},
        0,
        unit_archetype_id
    });
    return true;
}

std::uint32_t RtsWorld::reserveNextUnitId() {
    // generated ids skip any caller supplied ids that are still in use
    while (units_.find(next_generated_unit_id_) != units_.end()) {
        ++next_generated_unit_id_;
    }
    return next_generated_unit_id_++;
}

RtsWorld::UnitState* RtsWorld::findUnit(std::uint32_t unit_id) {
    const auto it = units_.find(unit_id);
    return it == units_.end() ? nullptr : &it->second;
}

const RtsWorld::UnitState* RtsWorld::findUnit(std::uint32_t unit_id) const {
    const auto it = units_.find(unit_id);
    return it == units_.end() ? nullptr : &it->second;
}

RtsWorld::OwnedBuildingState* RtsWorld::findOwnedBuilding(std::uint32_t building_id) {
    const auto it = owned_buildings_.find(building_id);
    return it == owned_buildings_.end() ? nullptr : &it->second;
}

const RtsWorld::OwnedBuildingState* RtsWorld::findOwnedBuilding(std::uint32_t building_id) const {
    const auto it = owned_buildings_.find(building_id);
    return it == owned_buildings_.end() ? nullptr : &it->second;
}

void RtsWorld::finishBuildingConstruction(OwnedBuildingState& building) {
    if (!building.under_construction) {
        return;
    }

    const BuildingInstance* building_instance = buildings_.findBuilding(building.building_id);
    if (!building_instance) {
        return;
    }

    // finishing construction activates production and optional tower behavior for this building
    // until this moment incomplete structures do not provide supply production or tower fire
    building.under_construction = false;
    building.build_progress = 1.0f;
    building.health = building.max_health;
    production_.registerBuilding(building.building_id, buildingCenter(*building_instance));

    const RtsBuildingArchetype* archetype = findBuildingArchetype(building.archetype_id);
    if (archetype && archetype->registers_tower) {
        addTower(building.building_id,
                 building.building_id,
                 building.team,
                 archetype->attack_range,
                 archetype->attack_damage,
                 archetype->attack_cooldown,
                 archetype->projectile_speed);
    }

    combat_events_.push_back(RtsCombatEvent{
        RtsCombatEventType::construction_completed,
        0,
        0,
        0,
        building.building_id,
        building.team,
        -1,
        buildingCenter(*building_instance),
        false,
        {},
        0,
        building.archetype_id
    });
}

void RtsWorld::emitResourceChangedEvent(int team,
                                        const std::string& resource_id,
                                        int amount,
                                        const glm::vec3& position) {
    if (resource_id.empty()) {
        return;
    }
    combat_events_.push_back(RtsEvent{
        RtsEventType::resources_changed,
        0,
        0,
        0,
        0,
        team,
        -1,
        position,
        false,
        resource_id,
        amount
    });
}

void RtsWorld::emitMatchEndedEventIfNeeded() {
    const std::optional<int> winner = winningTeam();
    if (!winner.has_value()) {
        last_reported_winner_.reset();
        return;
    }
    if (last_reported_winner_.has_value() && last_reported_winner_.value() == winner.value()) {
        return;
    }

    // emit the event only when the winner changes from not reported to a concrete team
    combat_events_.push_back(RtsCombatEvent{
        RtsCombatEventType::match_ended,
        0,
        0,
        0,
        0,
        winner.value(),
        winner.value(),
        glm::vec3(0.0f),
        false
    });
    last_reported_winner_ = winner;
}
