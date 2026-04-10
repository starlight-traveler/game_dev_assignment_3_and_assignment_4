/**
 * @file RtsWorld.h
 * @brief high level rts world that coordinates terrain units buildings ai combat and economy
 */
#ifndef RTS_WORLD_H
#define RTS_WORLD_H

#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

#include "BuildingSystem.h"
#include "FogOfWar.h"
#include "RtsAi.h"
#include "RtsCombat.h"
#include "RtsEconomy.h"
#include "RtsPathfinder.h"
#include "RtsProduction.h"
#include "RtsTypes.h"
#include "TerrainGrid.h"

/**
 * @brief central runtime coordinator for the full rts ruleset
 *
 * this class owns the main subsystems and the mutable simulation state
 * terrain and building occupancy
 * unit state and orders
 * economy production ai and combat
 * fog of war and match end detection
 *
 * most public functions either
 * configure static data
 * submit commands
 * or return readonly snapshots
 *
 * update is the main simulation tick where the actual game rules advance
 */
class RtsWorld {
public:
    /**
     * @brief Constructs an RTS world with its own terrain, building occupancy, and pathfinder
     * @param width Grid width in cells
     * @param height Grid height in cells
     * @param cell_size World-space cell size
     * @param origin_xz Minimum world-space XZ corner of cell (0, 0)
     */
    RtsWorld(int width,
             int height,
             float cell_size,
             const glm::vec2& origin_xz = glm::vec2(0.0f));

    // direct subsystem accessors mainly used by setup code and tests
    TerrainGrid& terrain();
    const TerrainGrid& terrain() const;
    BuildingSystem& buildings();
    const BuildingSystem& buildings() const;

    /**
     * @brief Registers or replaces one unit archetype by name
     * @param archetype_id Stable archetype id
     * @param archetype Gameplay stats
     * @return True when the archetype is valid and stored
     */
    bool registerUnitArchetype(const std::string& archetype_id, const RtsUnitArchetype& archetype);

    /**
     * @brief Looks up one unit archetype by name
     * @param archetype_id Archetype id
     * @return Archetype pointer or nullptr
     */
    const RtsUnitArchetype* findUnitArchetype(const std::string& archetype_id) const;

    /**
     * @brief Registers or replaces one building archetype by name
     * @param archetype_id Stable archetype id
     * @param archetype Gameplay and placement stats
     * @return True when the archetype is valid and stored
     */
    bool registerBuildingArchetype(const std::string& archetype_id,
                                   const RtsBuildingArchetype& archetype);

    /**
     * @brief Looks up one building archetype by name
     * @param archetype_id Archetype id
     * @return Archetype pointer or nullptr
     */
    const RtsBuildingArchetype* findBuildingArchetype(const std::string& archetype_id) const;

    /**
     * @brief Adds one logical unit with explicit stats
     */
    bool addUnit(std::uint32_t unit_id,
                 int team,
                 const glm::vec3& position,
                 float move_speed = 2.5f,
                 float radius = 0.35f,
                 float aggro_range = 3.5f,
                 float guard_radius = 1.5f,
                 float attack_range = 0.9f);

    /**
     * @brief Adds one logical unit from a registered archetype
     * @param unit_id Stable unit identifier
     * @param team Team id used for enemy detection
     * @param position Initial world position
     * @param archetype_id Registered archetype id
     * @return True when the unit was added
     */
    bool addUnitFromArchetype(std::uint32_t unit_id,
                              int team,
                              const glm::vec3& position,
                              const std::string& archetype_id);

    // unit lookup helpers
    bool removeUnit(std::uint32_t unit_id);
    bool hasUnit(std::uint32_t unit_id) const;
    std::size_t unitCount() const;
    glm::vec3 getUnitPosition(std::uint32_t unit_id) const;
    float unitHealth(std::uint32_t unit_id) const;
    bool isUnitAlive(std::uint32_t unit_id) const;
    std::optional<RtsWorldUnitSnapshot> getUnitSnapshot(std::uint32_t unit_id) const;

    /**
     * @brief Returns snapshots for all alive units
     * @return Unit snapshot list
     */
    std::vector<RtsWorldUnitSnapshot> unitSnapshots() const;

    // unit control state queries
    bool isUnitMoving(std::uint32_t unit_id) const;
    bool isHoldingPosition(std::uint32_t unit_id) const;
    std::size_t queuedOrderCount(std::uint32_t unit_id) const;
    std::optional<RtsOrderType> activeOrderType(std::uint32_t unit_id) const;

    /**
     * @brief Returns whether a registered building archetype can be placed at a cell
     * @param archetype_id Registered archetype id
     * @param anchor Minimum cell for the footprint
     * @return True when placement is valid
     */
    bool canPlaceBuildingFromArchetype(const std::string& archetype_id,
                                       const GridCoord& anchor) const;

    /**
     * @brief Places a building from a registered archetype and tracks ownership
     * @param team Owning team id
     * @param archetype_id Registered archetype id
     * @param anchor Minimum cell for the footprint
     * @return New building id or nullopt when placement fails
     */
    std::optional<std::uint32_t> placeBuildingFromArchetype(int team,
                                                            const std::string& archetype_id,
                                                            const GridCoord& anchor);

    /**
     * @brief Removes a tracked building by id
     * @param building_id Building id
     * @return True when removal succeeds
     */
    bool removeBuilding(std::uint32_t building_id);
    // building state queries and construction controls
    std::optional<RtsWorldBuildingSnapshot> getBuildingSnapshot(std::uint32_t building_id) const;
    float buildingHealth(std::uint32_t building_id) const;
    bool isBuildingComplete(std::uint32_t building_id) const;
    std::optional<std::uint32_t> startBuildingConstruction(int team,
                                                           const std::string& archetype_id,
                                                           const GridCoord& anchor,
                                                           std::uint32_t builder_unit_id,
                                                           bool spend_resources = true);
    bool cancelBuildingConstruction(std::uint32_t building_id, bool refund_resources = true);
    bool issueRepairOrder(std::uint32_t unit_id,
                          std::uint32_t building_id,
                          bool queue_when_busy = false);

    /**
     * @brief Returns snapshots for tracked placed buildings
     * @return Building snapshot list
     */
    std::vector<RtsWorldBuildingSnapshot> buildingSnapshots() const;

    /**
     * @brief Registers one auto-attacking tower bound to an existing building
     */
    bool addTower(std::uint32_t tower_id,
                  std::uint32_t building_id,
                  int team,
                  float attack_range = 4.5f,
                  float attack_damage = 12.0f,
                  float attack_cooldown = 1.0f,
                  float projectile_speed = 8.0f);
    // tower and projectile inspection helpers
    bool removeTower(std::uint32_t tower_id);
    bool hasTower(std::uint32_t tower_id) const;
    std::size_t towerCount() const;
    std::vector<RtsWorldProjectileSnapshot> projectileSnapshots() const;
    const std::vector<RtsEvent>& events() const;

    /**
     * @brief Sets one team resource balance to an absolute amount
     */
    void setTeamResourceAmount(int team, const std::string& resource_id, int amount);

    /**
     * @brief Adds a signed delta to one team resource balance
     * @return New stored amount
     */
    int addTeamResourceAmount(int team, const std::string& resource_id, int delta);

    /**
     * @brief Looks up one team resource balance
     */
    int teamResourceAmount(int team, const std::string& resource_id) const;

    /**
     * @brief Returns stored resource balances for one team
     */
    std::vector<RtsTeamResourceSnapshot> teamResourceSnapshots(int team) const;

    /**
     * @brief Returns whether a team can afford one set of costs
     */
    bool canAffordCosts(int team, const std::vector<RtsResourceCost>& costs) const;

    /**
     * @brief Spends one set of costs from a team balance when affordable
     */
    bool spendTeamResources(int team, const std::vector<RtsResourceCost>& costs);

    /**
     * @brief Refunds one set of costs to a team balance
     */
    void refundTeamResources(int team, const std::vector<RtsResourceCost>& costs);

    /**
     * @brief Returns current used supply for one team
     */
    int teamSupplyUsed(int team) const;

    /**
     * @brief Returns current provided supply for one team
     */
    int teamSupplyProvided(int team) const;

    /**
     * @brief Adds one finite resource node to the world
     * @return New node id or nullopt when invalid
     */
    std::optional<std::uint32_t> addResourceNode(const std::string& resource_id,
                                                 const GridCoord& cell,
                                                 int amount);

    /**
     * @brief Removes one resource node by id
     */
    bool removeResourceNode(std::uint32_t node_id);

    /**
     * @brief Returns remaining amount for one resource node
     */
    int resourceNodeAmount(std::uint32_t node_id) const;

    /**
     * @brief Removes up to the requested amount from one resource node
     * @return Amount actually harvested
     */
    int harvestResourceNode(std::uint32_t node_id, int requested_amount);

    /**
     * @brief Returns snapshots for all resource nodes
     */
    std::vector<RtsWorldResourceNodeSnapshot> resourceNodeSnapshots() const;

    /**
     * @brief Returns whether one building can enqueue production of one unit archetype
     */
    bool canProduceUnitFromBuilding(std::uint32_t building_id,
                                    const std::string& unit_archetype_id) const;

    /**
     * @brief Enqueues one unit at a production-capable building and spends its costs
     */
    bool enqueueProduction(std::uint32_t building_id, const std::string& unit_archetype_id);

    /**
     * @brief Cancels the last queued production entry
     */
    bool cancelLastProduction(std::uint32_t building_id, bool refund_resources = true);

    /**
     * @brief Clears a building production queue
     */
    bool clearProductionQueue(std::uint32_t building_id, bool refund_resources = true);

    /**
     * @brief Sets the rally point used by produced units
     */
    bool setProductionRallyPoint(std::uint32_t building_id, const glm::vec3& rally_point);

    /**
     * @brief Returns the rally point used by produced units
     */
    glm::vec3 productionRallyPoint(std::uint32_t building_id) const;

    /**
     * @brief Returns production queue snapshots for owned buildings
     */
    std::vector<RtsWorldProductionSnapshot> productionSnapshots() const;

    // specialized helpers used by ui demos and tests
    bool issueHarvestOrder(std::uint32_t unit_id, std::uint32_t resource_node_id);
    void setAiProfile(int team, const RtsAiProfile& profile);
    bool removeAiProfile(int team);
    bool hasAiProfile(int team) const;
    const RtsAiProfile* aiProfile(int team) const;

    /**
     * @brief Returns combat events emitted during the most recent update
     * @return Event list
     */
    const std::vector<RtsCombatEvent>& combatEvents() const;

    /**
     * @brief Returns the fog of war grid
     * @return FogOfWar reference
     */
    const FogOfWar& fog() const;

    /**
     * @brief Returns visibility state for a cell for a team
     * @param team Team id
     * @param cell Grid coordinate
     * @return Visibility state
     */
    VisibilityState cellVisibilityForTeam(int team, const GridCoord& cell) const;

    /**
     * @brief Returns whether a unit is visible to a team
     * @param team Team id
     * @param unit_id Unit id
     * @return True when visible
     */
    bool isUnitVisibleToTeam(int team, std::uint32_t unit_id) const;

    /**
     * @brief Returns whether a building is visible to a team
     * @param team Team id
     * @param building_id Building id
     * @return True when visible
     */
    bool isBuildingVisibleToTeam(int team, std::uint32_t building_id) const;

    // match state helpers
    std::optional<int> winningTeam() const;
    bool isMatchOver() const;

    /**
     * @brief Issues one command to a unit, optionally appending instead of replacing
     */
    bool issueOrder(std::uint32_t unit_id,
                    const RtsOrder& order,
                    bool queue_when_busy = false);

    /**
     * @brief Issues one formation-style order to a group of units
     * @param unit_ids Units to command
     * @param center Formation center in world space
     * @param order_type Usually move or attack_move
     * @param spacing World-space gap between units
     * @param queue_when_busy When true appends behind current orders
     * @return True when at least one unit accepted the order
     */
    bool issueFormationOrder(const std::vector<std::uint32_t>& unit_ids,
                             const glm::vec3& center,
                             RtsOrderType order_type = RtsOrderType::move,
                             float spacing = 1.2f,
                             bool queue_when_busy = false,
                             std::uint32_t target_unit_id = 0);

    // simulation control
    bool clearOrders(std::uint32_t unit_id);
    void update(float delta_seconds);

private:
    /**
     * @brief full mutable runtime state for one unit
     *
     * snapshots are derived from this
     * the update loop mutates these fields directly during movement combat and order handling
     */
    struct UnitState {
        // identity and ownership
        std::uint32_t unit_id;
        int team;
        std::string archetype_id;
        // movement and combat stats copied from an archetype or explicit addUnit call
        glm::vec3 position;
        float move_speed;
        float radius;
        float aggro_range;
        float guard_radius;
        float attack_range;
        float max_health;
        float health;
        float attack_damage;
        float attack_cooldown;
        float attack_cooldown_remaining;
        float projectile_speed;
        float harvest_cooldown;
        float harvest_cooldown_remaining;
        float recent_hit_timer;
        // carried worker payload
        std::string carried_resource_id;
        int carried_resource_amount;
        // command and path following state
        bool moving;
        bool holding_position;
        std::deque<RtsOrder> order_queue;
        std::optional<RtsOrder> active_order;
        std::vector<glm::vec3> path_points;
        std::size_t path_index;
        GridCoord goal_cell;
        bool has_goal_cell;
    };

    /**
     * @brief mutable runtime state for one placed owned building
     */
    struct OwnedBuildingState {
        std::uint32_t building_id;
        int team;
        std::string archetype_id;
        float health;
        float max_health;
        float build_progress;
        float build_time;
        float repair_rate;
        bool under_construction;
        std::vector<RtsResourceCost> cost;
    };

    // low level movement and target search helpers
    bool isCellTraversable(const GridCoord& cell) const;
    bool findNearestTraversableCell(const GridCoord& start_cell, GridCoord& out_cell) const;
    void clearUnitMotion(UnitState& unit);
    bool assignPath(UnitState& unit,
                    const glm::vec3& target_position,
                    float arrival_radius);
    bool moveUnitToward(UnitState& unit,
                        const glm::vec3& target_position,
                        float move_speed,
                        float arrival_radius,
                        float delta_seconds);
    bool tryAttackUnit(UnitState& attacker, const UnitState& target);
    bool tryAttackBuilding(UnitState& attacker, const OwnedBuildingState& target);
    std::optional<std::uint32_t> findNearestEnemyUnit(int team,
                                                      const glm::vec3& center,
                                                      float radius) const;
    std::optional<std::uint32_t> findNearestEnemyBuilding(int team,
                                                          const glm::vec3& center,
                                                          float radius) const;
    std::optional<std::uint32_t> findNearestFriendlyDropoffBuilding(int team,
                                                                    const glm::vec3& center) const;
    bool handleHarvestOrder(UnitState& unit, RtsOrder& order, float delta_seconds);
    bool handleConstructOrder(UnitState& unit, RtsOrder& order, float delta_seconds);
    bool handleRepairOrder(UnitState& unit, RtsOrder& order, float delta_seconds);
    void cleanupDestroyedEntities();

    // building geometry helpers used for interaction ranges and projectile targeting
    glm::vec3 buildingCenter(const BuildingInstance& building) const;
    float buildingInteractionRadius(const BuildingInstance& building) const;

    // order machine helpers
    void pullNextQueuedOrder(UnitState& unit);
    void updateActiveOrder(UnitState& unit, float delta_seconds);

    // production helpers
    bool buildingCanProduceUnit(const OwnedBuildingState& building,
                                const std::string& unit_archetype_id) const;
    bool spawnProducedUnit(std::uint32_t building_id,
                           const std::string& unit_archetype_id,
                           const glm::vec3& rally_point);

    // internal lookups and state transitions
    std::uint32_t reserveNextUnitId();
    UnitState* findUnit(std::uint32_t unit_id);
    const UnitState* findUnit(std::uint32_t unit_id) const;
    OwnedBuildingState* findOwnedBuilding(std::uint32_t building_id);
    const OwnedBuildingState* findOwnedBuilding(std::uint32_t building_id) const;
    void finishBuildingConstruction(OwnedBuildingState& building);

    // event and fog helpers
    void emitMatchEndedEventIfNeeded();
    void emitResourceChangedEvent(int team,
                                  const std::string& resource_id,
                                  int amount,
                                  const glm::vec3& position = glm::vec3(0.0f));
    void updateFogOfWar();

    // core subsystem instances
    TerrainGrid terrain_;
    FogOfWar fog_;
    BuildingSystem buildings_;
    RtsPathfinder pathfinder_;
    // static gameplay templates
    std::unordered_map<std::string, RtsUnitArchetype> unit_archetypes_;
    std::unordered_map<std::string, RtsBuildingArchetype> building_archetypes_;
    // mutable world state
    RtsEconomySystem economy_;
    std::unordered_map<std::uint32_t, UnitState> units_;
    std::unordered_map<std::uint32_t, OwnedBuildingState> owned_buildings_;
    RtsProductionSystem production_;
    RtsAiSystem ai_;
    RtsCombatSystem combat_;
    // events collected during the most recent update tick
    std::vector<RtsCombatEvent> combat_events_;
    // generator for produced units that do not have caller supplied ids
    std::uint32_t next_generated_unit_id_;
    // used so match_ended is only emitted once per winner
    std::optional<int> last_reported_winner_;
};

#endif
