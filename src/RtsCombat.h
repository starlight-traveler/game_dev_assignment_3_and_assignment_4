/**
 * @file RtsCombat.h
 * @brief projectile and tower combat state for the rts layer
 */
#ifndef RTS_COMBAT_H
#define RTS_COMBAT_H

#include <cstdint>
#include <functional>
#include <optional>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

#include "RtsTypes.h"

/**
 * @brief owns tower state projectile flight and projectile updates
 *
 * this system does not know the full world on its own
 * instead callers provide lookup callbacks for targets and impact handling
 */
class RtsCombatSystem {
public:
    /**
     * @brief minimal readonly target data needed by the combat system
     *
     * combat does not own units or buildings directly
     * so callers adapt world state into this small common shape
     */
    struct TargetSnapshot {
        // world id of the target
        std::uint32_t target_id;
        // owning team used for enemy checks outside this struct
        int team;
        // current target center in world space
        glm::vec3 position;
        // approximate hit radius for impact tests
        float radius;
        // whether the target still exists and can be hit
        bool alive;
        // tells combat whether target_id refers to a unit or a building
        bool is_building;
    };

    /**
     * @brief one autonomous tower attacker
     */
    struct TowerState {
        // autonomous attacker id used by combat events and projectile source ids
        std::uint32_t tower_id;
        // building that physically owns this tower logic
        std::uint32_t building_id;
        // owning team id
        int team;
        // planar firing range before target radius is added
        float attack_range;
        // damage per successful hit
        float attack_damage;
        // delay between consecutive shots
        float attack_cooldown;
        // countdown until the tower may fire again
        float attack_cooldown_remaining;
        // speed given to spawned projectiles
        float projectile_speed;
    };

    /**
     * @brief one in flight projectile
     */
    struct ProjectileState {
        // unique projectile id
        std::uint32_t projectile_id;
        // firing unit or tower id
        std::uint32_t source_id;
        // unit target if nonzero
        std::uint32_t target_unit_id;
        // building target if nonzero
        std::uint32_t target_building_id;
        // source building id when the projectile came from a tower
        std::uint32_t source_building_id;
        // allegiance used by downstream systems and events
        int team;
        // current projectile position
        glm::vec3 position;
        // movement speed in world units per second
        float speed;
        // damage to apply on impact
        float damage;
        // whether this projectile came from tower logic instead of a unit attack
        bool from_tower;
    };

    RtsCombatSystem();

    // tower registration helpers
    bool addTower(std::uint32_t tower_id,
                  std::uint32_t building_id,
                  int team,
                  float attack_range,
                  float attack_damage,
                  float attack_cooldown,
                  float projectile_speed);
    bool removeTower(std::uint32_t tower_id);
    bool hasTower(std::uint32_t tower_id) const;
    std::size_t towerCount() const;
    std::vector<TowerState> towerStates() const;

    /**
     * @brief spawns one projectile and emits a projectile_spawned event
     */
    bool spawnProjectile(std::uint32_t source_id,
                         std::uint32_t target_unit_id,
                         std::uint32_t target_building_id,
                         std::uint32_t source_building_id,
                         int team,
                         const glm::vec3& source_position,
                         float projectile_speed,
                         float damage,
                         bool from_tower,
                         std::vector<RtsEvent>& events);

    /**
     * @brief exports projectile state for rendering
     *
     * target_position_lookup lets the renderer draw projectiles toward moving targets
     */
    std::vector<RtsWorldProjectileSnapshot> projectileSnapshots(
        const std::function<std::optional<glm::vec3>(const ProjectileState&)>&
            target_position_lookup) const;

    /**
     * @brief advances all projectiles and invokes impact_handler on hits
     */
    void updateProjectiles(
        float delta_seconds,
        const std::function<std::optional<TargetSnapshot>(const ProjectileState&)>& target_lookup,
        const std::function<void(const ProjectileState&, const TargetSnapshot&, std::vector<RtsEvent>&)>&
            impact_handler,
        std::vector<RtsEvent>& events);

    /**
     * @brief updates tower cooldowns target selection and firing
     */
    void updateTowers(
        float delta_seconds,
        const std::function<std::optional<glm::vec3>(std::uint32_t)>& building_position_lookup,
        const std::function<std::optional<TargetSnapshot>(int, const glm::vec3&, float)>&
            nearest_enemy_lookup,
        std::vector<RtsEvent>& events);

private:
    // keyed by tower id because towers are long lived building bound attackers
    std::unordered_map<std::uint32_t, TowerState> towers_;
    // projectiles are short lived so a flat vector works well and keeps updates simple
    std::vector<ProjectileState> projectiles_;
    // monotonically increasing projectile id source
    std::uint32_t next_projectile_id_;
};

#endif
