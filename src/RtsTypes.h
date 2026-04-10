/**
 * @file RtsTypes.h
 * @brief shared rts enums config records snapshot types and event payloads
 */
#ifndef RTS_TYPES_H
#define RTS_TYPES_H

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "BuildingSystem.h"
#include "TerrainGrid.h"

/**
 * @brief all unit order kinds understood by the rts world
 *
 * move means travel to a point then stop
 * attack_move means travel toward a point but break to fight enemies on the way
 * patrol means bounce between two points
 * guard means stay near a friendly unit and react to nearby enemies
 * stop means cancel everything and become idle
 * hold_position means stop moving but still shoot enemies in range
 * harvest means gather from a resource node and return to a dropoff
 * construct means build an unfinished building
 * repair means restore health on a damaged building
 */
enum class RtsOrderType {
    move,
    attack_move,
    patrol,
    guard,
    stop,
    hold_position,
    harvest,
    construct,
    repair
};

/**
 * @brief one queued or active command for a unit
 *
 * different fields matter for different order types
 * for example
 * move uses target_position
 * patrol uses both target positions
 * guard uses target_unit_id
 * harvest uses target_resource_node_id
 * repair and construct use target_building_id
 */
struct RtsOrder {
    // which branch of the order state machine to run
    RtsOrderType type = RtsOrderType::move;
    // main destination point or contextual target point
    glm::vec3 target_position = glm::vec3(0.0f);
    // second endpoint used mainly by patrol
    glm::vec3 secondary_target_position = glm::vec3(0.0f);
    // explicit unit focus for guard or attack style orders
    std::uint32_t target_unit_id = 0;
    // optional per order move speed override
    float move_speed = 0.0f;
    // snap or interaction distance for movement completion
    float arrival_radius = 0.0f;
    // target resource patch for harvest orders
    std::uint32_t target_resource_node_id = 0;
    // target building for construct repair or focused attack style behavior
    std::uint32_t target_building_id = 0;
};

/**
 * @brief one resource requirement entry used by units or buildings
 */
struct RtsResourceCost {
    // resource key like gold wood or similar
    std::string resource_id;
    // amount of that resource required
    int amount = 0;
};

/**
 * @brief gameplay stats template for one unit type
 *
 * these values get copied into UnitState when a unit is spawned
 * so archetypes act like reusable blueprints
 */
struct RtsUnitArchetype {
    // base travel speed for this unit type
    float move_speed = 2.5f;
    // simple collision and spacing radius
    float radius = 0.35f;
    // how far the unit can notice enemies and become interested in fighting
    float aggro_range = 3.5f;
    // preferred leash distance when following a guard target
    float guard_radius = 1.5f;
    // maximum attack distance before movement is needed
    float attack_range = 0.9f;
    // starting and maximum health value for spawned units
    float max_health = 100.0f;
    // damage dealt by one successful attack or projectile impact
    float attack_damage = 18.0f;
    // minimum time between attacks
    float attack_cooldown = 0.8f;
    // projectile travel speed for ranged attacks
    float projectile_speed = 8.5f;
    // resource price to create the unit
    std::vector<RtsResourceCost> cost{};
    // time spent in a production queue before the unit appears
    float production_time = 0.0f;
    // population usage for supply cap checks
    int supply_cost = 0;
    // whether this archetype can gather resources
    bool can_harvest = false;
    // maximum carried resource amount before a return trip is needed
    int carry_capacity = 0;
    // amount gathered per harvest cycle
    int harvest_amount = 0;
    // wait time between harvest actions
    float harvest_cooldown = 0.0f;
    // fog of war reveal range around the unit
    float vision_range = 5.0f;
};

/**
 * @brief gameplay and placement template for one building type
 *
 * placement describes footprint and collision rules
 * the rest controls economy production vision construction and optional tower combat
 */
struct RtsBuildingArchetype {
    // footprint and placement rules reused by the building system
    BuildingDefinition placement{};
    // whether losing all of these can cause defeat
    bool counts_for_victory = true;
    // whether the combat system should treat this as a tower attacker
    bool registers_tower = false;
    // tower or defensive attack range if enabled
    float attack_range = 0.0f;
    // damage dealt per defensive shot
    float attack_damage = 0.0f;
    // minimum delay between building attacks
    float attack_cooldown = 0.0f;
    // projectile speed for tower shots
    float projectile_speed = 0.0f;
    // resource price to place the building
    std::vector<RtsResourceCost> cost{};
    // unit archetypes this building may queue for production
    std::vector<std::string> producible_unit_archetypes{};
    // supply granted to the owning team once the building is complete
    int supply_provided = 0;
    // whether workers may return harvested resources here
    bool accepts_resource_dropoff = false;
    // finished building hit points
    float max_health = 350.0f;
    // time workers must spend to complete construction
    float build_time = 0.0f;
    // health restored per second by repair work
    float repair_rate = 28.0f;
    // fog of war reveal range around the building
    float vision_range = 6.0f;
};

/**
 * @brief read only unit state exported from the world for ui ai and tests
 *
 * this is a snapshot not a live mutable unit reference
 * callers can inspect it freely without risking world corruption
 */
struct RtsWorldUnitSnapshot {
    // stable world id for lookups and commands
    std::uint32_t unit_id = 0;
    // owning team id
    int team = 0;
    // blueprint name used to create the unit
    std::string archetype_id{};
    // current world position
    glm::vec3 position = glm::vec3(0.0f);
    // approximate unit footprint radius
    float radius = 0.0f;
    // current health
    float health = 0.0f;
    // maximum health for health bar ratios
    float max_health = 0.0f;
    // whether the unit is currently traveling under order or steering
    bool moving = false;
    // whether hold position mode is active
    bool holding_position = false;
    // how many queued orders remain after the active one
    std::size_t queued_orders = 0;
    // currently executing order if one exists
    std::optional<RtsOrderType> active_order = std::nullopt;
    // short timer used by rendering or ui feedback after taking damage
    float recent_hit_timer = 0.0f;
    // resource type currently carried by the worker
    std::string carried_resource_id{};
    // carried amount for worker return logic
    int carried_resource_amount = 0;
};

/**
 * @brief read only building state exported from the world
 *
 * center is the average world position of the occupied footprint
 * construction_progress stays in 0 to 1 while under_construction is true
 */
struct RtsWorldBuildingSnapshot {
    // stable world id for lookups and commands
    std::uint32_t building_id = 0;
    // owning team id
    int team = 0;
    // blueprint name used to create the building
    std::string archetype_id{};
    // minimum occupied terrain cell of the footprint
    GridCoord anchor{};
    // footprint size in cells along x
    int footprint_width = 0;
    // footprint size in cells along y or world z
    int footprint_height = 0;
    // average world position used for ui markers and unit targeting
    glm::vec3 center = glm::vec3(0.0f);
    // whether units are blocked from walking through the footprint
    bool blocks_movement = false;
    // whether this building matters for win loss checks
    bool counts_for_victory = false;
    // whether this building participates in automated tower combat
    bool registers_tower = false;
    // current health
    float health = 0.0f;
    // maximum health when fully completed
    float max_health = 0.0f;
    // normalized build progress from zero to one
    float construction_progress = 1.0f;
    // whether the building still needs worker build time
    bool under_construction = false;
    // quick completed flag so callers do not have to infer from progress
    bool completed = true;
};

/**
 * @brief read only projectile state exported for rendering and debugging
 *
 * target_position is resolved at snapshot time
 * it may move from frame to frame because the target may be moving too
 */
struct RtsWorldProjectileSnapshot {
    // stable world id for this projectile
    std::uint32_t projectile_id = 0;
    // firing unit or tower id
    std::uint32_t source_id = 0;
    // targeted unit if this is a unit projectile
    std::uint32_t target_unit_id = 0;
    // owner or allegiance of the projectile
    int team = 0;
    // current world position
    glm::vec3 position = glm::vec3(0.0f);
    // current sampled aim point used by rendering
    glm::vec3 target_position = glm::vec3(0.0f);
    // whether the shot came from a tower style building
    bool from_tower = false;
    // targeted building if this is a building projectile
    std::uint32_t target_building_id = 0;
};

/**
 * @brief one stored team resource amount
 */
struct RtsTeamResourceSnapshot {
    // resource type name
    std::string resource_id;
    // current stored amount for the team
    int amount = 0;
};

/**
 * @brief one queued production entry in snapshot form
 *
 * active marks the front queue item that is currently counting down
 */
struct RtsWorldProductionEntrySnapshot {
    // archetype that will be spawned when this queue entry finishes
    std::string unit_archetype_id{};
    // countdown still remaining for this entry
    float remaining_time = 0.0f;
    // whether this is the front queue item currently ticking down
    bool active = false;
};

/**
 * @brief production queue snapshot for one building
 *
 * queue order matters
 * only the first entry is actively progressing at a time
 */
struct RtsWorldProductionSnapshot {
    // building that owns this queue
    std::uint32_t building_id = 0;
    // owning team id
    int team = 0;
    // type of building running the queue
    std::string building_archetype_id{};
    // point newly produced units try to move toward
    glm::vec3 rally_point = glm::vec3(0.0f);
    // queue contents in current production order
    std::vector<RtsWorldProductionEntrySnapshot> queue{};
};

/**
 * @brief read only resource node state for ui ai and tests
 *
 * center is the world position workers move toward
 * remaining_amount drops as workers harvest from the node
 */
struct RtsWorldResourceNodeSnapshot {
    // stable world id for the node
    std::uint32_t node_id = 0;
    // resource type yielded by the node
    std::string resource_id{};
    // terrain cell containing the node
    GridCoord cell{};
    // world location workers move toward to harvest
    glm::vec3 center = glm::vec3(0.0f);
    // amount still available before the node is exhausted
    int remaining_amount = 0;
};

/**
 * @brief lightweight ai behavior knobs for one team
 *
 * this keeps the ai simple
 * instead of hardcoding every decision
 * the world can give each team one profile
 */
struct RtsAiProfile {
    // delay between high level ai decision passes
    float think_interval = 0.5f;
    // target minimum worker count before greed slows down
    int minimum_workers = 2;
    // desired army size before planned attacks start
    int attack_force_size = 4;
    // distance around owned positions where defense is prioritized
    float defend_radius = 7.5f;
    // whether idle workers should be auto assigned to economy tasks
    bool auto_harvest = true;
    // whether the ai should automatically queue units
    bool auto_produce = true;
    // whether the ai should automatically launch and react in combat
    bool auto_attack = true;
    // worker blueprint id used when the ai wants more economy units
    std::string worker_archetype_id{};
    // preferred unit archetypes in rough queue order
    std::vector<std::string> production_priority{};
};

/**
 * @brief all event kinds that the rts world can emit during updates
 *
 * these events are mainly for ui feedback tests and demo hooks
 * they describe what happened during the most recent simulation tick
 */
enum class RtsEventType {
    projectile_spawned,
    projectile_hit,
    unit_died,
    building_damaged,
    building_destroyed,
    building_repaired,
    tower_fired,
    building_placed,
    building_removed,
    construction_started,
    construction_completed,
    construction_canceled,
    match_ended,
    production_started,
    production_completed,
    production_canceled,
    unit_spawned,
    resources_changed,
    resource_harvested,
    resources_deposited
};

/**
 * @brief one event payload emitted by combat production economy or match flow
 *
 * not every field is meaningful for every event type
 * for example
 * projectile events use projectile_id and source_id
 * resource events use resource_id and amount
 * match_ended uses winner_team
 */
struct RtsEvent {
    // what happened
    RtsEventType type = RtsEventType::projectile_spawned;
    // usually attacker builder or acting unit id
    std::uint32_t source_id = 0;
    // generic target id used by many events
    std::uint32_t target_id = 0;
    // projectile involved in the event if any
    std::uint32_t projectile_id = 0;
    // building associated with the event if any
    std::uint32_t building_id = 0;
    // acting or owning team
    int team = -1;
    // only meaningful for match_ended
    int winner_team = -1;
    // world position where the event happened when relevant
    glm::vec3 position = glm::vec3(0.0f);
    // whether the event came from a tower source
    bool from_tower = false;
    // resource key for economy events
    std::string resource_id{};
    // amount for economy events
    int amount = 0;
    // unit or building archetype id for production and construction events
    std::string archetype_id{};
};

// combat system and world currently share the same event type
using RtsCombatEventType = RtsEventType;
using RtsCombatEvent = RtsEvent;

#endif
