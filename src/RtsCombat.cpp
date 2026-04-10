#include "RtsCombat.h"

#include <algorithm>
#include <cmath>

namespace {
constexpr float kMinAttackDamage = 0.01f;
constexpr float kMinAttackCooldown = 0.01f;
constexpr float kMinProjectileSpeed = 0.01f;

float planar_distance(const glm::vec3& a, const glm::vec3& b) {
    return glm::length(glm::vec2(a.x - b.x, a.z - b.z));
}
}  // namespace

RtsCombatSystem::RtsCombatSystem()
    // projectile ids start at one so zero can remain a clear invalid sentinel
    : towers_(),
      projectiles_(),
      next_projectile_id_(1) {}

bool RtsCombatSystem::addTower(std::uint32_t tower_id,
                               std::uint32_t building_id,
                               int team,
                               float attack_range,
                               float attack_damage,
                               float attack_cooldown,
                               float projectile_speed) {
    if (towers_.find(tower_id) != towers_.end() ||
        building_id == 0 ||
        attack_range <= 0.0f ||
        attack_damage < kMinAttackDamage ||
        attack_cooldown < kMinAttackCooldown ||
        projectile_speed < kMinProjectileSpeed) {
        return false;
    }

    // a new tower starts ready to fire because cooldown_remaining begins at zero
    towers_[tower_id] = TowerState{
        tower_id,
        building_id,
        team,
        attack_range,
        attack_damage,
        attack_cooldown,
        0.0f,
        projectile_speed
    };
    return true;
}

bool RtsCombatSystem::removeTower(std::uint32_t tower_id) {
    const bool removed = towers_.erase(tower_id) > 0;
    if (!removed) {
        return false;
    }

    // also clear any old tower owned projectiles so they do not outlive the tower source
    projectiles_.erase(
        std::remove_if(projectiles_.begin(), projectiles_.end(),
                       [tower_id](const ProjectileState& projectile) {
                           return projectile.from_tower && projectile.source_id == tower_id;
                       }),
        projectiles_.end());
    return true;
}

bool RtsCombatSystem::hasTower(std::uint32_t tower_id) const {
    return towers_.find(tower_id) != towers_.end();
}

std::size_t RtsCombatSystem::towerCount() const {
    return towers_.size();
}

std::vector<RtsCombatSystem::TowerState> RtsCombatSystem::towerStates() const {
    std::vector<TowerState> states{};
    states.reserve(towers_.size());
    for (const auto& entry : towers_) {
        states.push_back(entry.second);
    }
    return states;
}

bool RtsCombatSystem::spawnProjectile(std::uint32_t source_id,
                                      std::uint32_t target_unit_id,
                                      std::uint32_t target_building_id,
                                      std::uint32_t source_building_id,
                                      int team,
                                      const glm::vec3& source_position,
                                      float projectile_speed,
                                      float damage,
                                      bool from_tower,
                                      std::vector<RtsEvent>& events) {
    if ((target_unit_id == 0 && target_building_id == 0) ||
        projectile_speed < kMinProjectileSpeed ||
        damage < kMinAttackDamage) {
        return false;
    }

    const std::uint32_t projectile_id = next_projectile_id_++;
    // projectile state keeps only the data needed to keep flying toward a moving target
    // only one of target_unit_id or target_building_id should be nonzero
    projectiles_.push_back(ProjectileState{
        projectile_id,
        source_id,
        target_unit_id,
        target_building_id,
        source_building_id,
        team,
        source_position,
        projectile_speed,
        damage,
        from_tower
    });
    events.push_back(RtsEvent{
        RtsEventType::projectile_spawned,
        source_id,
        // target_id uses whichever target kind is active so listeners have one common field to read
        target_unit_id != 0 ? target_unit_id : target_building_id,
        projectile_id,
        source_building_id,
        team,
        -1,
        source_position,
        from_tower
    });
    return true;
}

std::vector<RtsWorldProjectileSnapshot> RtsCombatSystem::projectileSnapshots(
    const std::function<std::optional<glm::vec3>(const ProjectileState&)>&
        target_position_lookup) const {
    std::vector<RtsWorldProjectileSnapshot> snapshots{};
    snapshots.reserve(projectiles_.size());
    for (const ProjectileState& projectile : projectiles_) {
        glm::vec3 target_position = projectile.position;
        const auto resolved = target_position_lookup(projectile);
        if (resolved.has_value()) {
            // caller can provide a live target position so the snapshot points where the projectile is headed now
            target_position = resolved.value();
        }
        snapshots.push_back(RtsWorldProjectileSnapshot{
            projectile.projectile_id,
            projectile.source_id,
            projectile.target_unit_id,
            projectile.team,
            projectile.position,
            target_position,
            projectile.from_tower,
            projectile.target_building_id
        });
    }
    return snapshots;
}

void RtsCombatSystem::updateProjectiles(
    float delta_seconds,
    const std::function<std::optional<TargetSnapshot>(const ProjectileState&)>& target_lookup,
    const std::function<void(const ProjectileState&, const TargetSnapshot&, std::vector<RtsEvent>&)>&
        impact_handler,
    std::vector<RtsEvent>& events) {
    if (projectiles_.empty()) {
        return;
    }

    std::vector<ProjectileState> surviving{};
    surviving.reserve(projectiles_.size());
    for (ProjectileState projectile : projectiles_) {
        // resolve the current moving target each frame
        const std::optional<TargetSnapshot> target = target_lookup(projectile);
        if (!target.has_value() || !target->alive) {
            // projectiles vanish if the target no longer exists
            continue;
        }

        const glm::vec3 to_target = target->position - projectile.position;
        const float distance = glm::length(to_target);
        const float impact_radius = target->radius + 0.05f;
        const float max_step = projectile.speed * delta_seconds;
        // if the projectile would reach or cross the target this frame trigger impact immediately
        // the small extra radius makes hits feel less brittle than aiming for a mathematical point center
        if (distance <= impact_radius || max_step >= std::max(distance - impact_radius, 0.0f)) {
            impact_handler(projectile, target.value(), events);
            continue;
        }

        // otherwise move it along the normalized direction toward the target
        projectile.position += (to_target / distance) * max_step;
        surviving.push_back(projectile);
    }
    projectiles_.swap(surviving);
}

void RtsCombatSystem::updateTowers(
    float delta_seconds,
    const std::function<std::optional<glm::vec3>(std::uint32_t)>& building_position_lookup,
    const std::function<std::optional<TargetSnapshot>(int, const glm::vec3&, float)>&
        nearest_enemy_lookup,
    std::vector<RtsEvent>& events) {
    std::vector<std::uint32_t> invalid_towers{};
    for (auto& entry : towers_) {
        TowerState& tower = entry.second;
        // cooldown counts down every frame
        tower.attack_cooldown_remaining =
            std::max(0.0f, tower.attack_cooldown_remaining - delta_seconds);

        const std::optional<glm::vec3> tower_position =
            building_position_lookup(tower.building_id);
        if (!tower_position.has_value()) {
            // if the backing building disappeared the tower should be removed
            invalid_towers.push_back(tower.tower_id);
            continue;
        }

        const std::optional<TargetSnapshot> target =
            nearest_enemy_lookup(tower.team, tower_position.value(), tower.attack_range);
        if (!target.has_value() || !target->alive) {
            // no target means the tower simply stays ready until something enters range
            continue;
        }
        // range check and cooldown check gate the actual shot
        if (planar_distance(tower_position.value(), target->position) >
                (tower.attack_range + target->radius) ||
            tower.attack_cooldown_remaining > 0.0f) {
            continue;
        }

        // emit a tower_fired event before the projectile is actually spawned
        // this lets listeners react to the firing moment even if the projectile is handled separately
        const std::uint32_t projectile_id = next_projectile_id_;
        events.push_back(RtsEvent{
            RtsEventType::tower_fired,
            tower.tower_id,
            target->target_id,
            projectile_id,
            tower.building_id,
            tower.team,
            -1,
            tower_position.value(),
            true
        });
        spawnProjectile(tower.tower_id,
                        target->is_building ? 0 : target->target_id,
                        target->is_building ? target->target_id : 0,
                        tower.building_id,
                        tower.team,
                        tower_position.value(),
                        tower.projectile_speed,
                        tower.attack_damage,
                        true,
                        events);
        tower.attack_cooldown_remaining = tower.attack_cooldown;
    }

    // remove dead tower entries after the iteration finishes
    for (const std::uint32_t tower_id : invalid_towers) {
        towers_.erase(tower_id);
    }
}
