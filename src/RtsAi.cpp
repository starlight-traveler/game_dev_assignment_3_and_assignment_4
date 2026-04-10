#include "RtsAi.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace {
// helper distance in ground space since most rts reasoning ignores height
float planar_distance(const glm::vec3& a, const glm::vec3& b) {
    return glm::length(glm::vec2(a.x - b.x, a.z - b.z));
}

// quick name based heuristic for identifying worker style archetypes
bool contains_worker_token(const std::string& archetype_id) {
    return archetype_id.find("worker") != std::string::npos ||
           archetype_id.find("harvest") != std::string::npos;
}
}

void RtsAiSystem::setTeamProfile(int team, const RtsAiProfile& profile) {
    // resetting state here keeps ai memory consistent with the new profile
    teams_[team] = TeamState{profile, 0.0f, 0.0f, 0U, 1.0f, false, glm::vec3(0.0f)};
}

bool RtsAiSystem::removeTeamProfile(int team) {
    return teams_.erase(team) > 0;
}

bool RtsAiSystem::hasTeamProfile(int team) const {
    return teams_.find(team) != teams_.end();
}

const RtsAiProfile* RtsAiSystem::teamProfile(int team) const {
    const auto it = teams_.find(team);
    return it == teams_.end() ? nullptr : &it->second.profile;
}

std::vector<RtsAiCommand> RtsAiSystem::update(float delta_seconds, const RtsAiFrame& frame) {
    std::vector<RtsAiCommand> commands{};
    if (delta_seconds <= 0.0f) {
        return commands;
    }

    // each team thinks independently and emits a list of desired high level commands
    for (auto& entry : teams_) {
        const int team = entry.first;
        TeamState& state = entry.second;
        // cooldowns let the ai think and launch attack waves at human readable intervals
        state.think_cooldown_remaining =
            std::max(0.0f, state.think_cooldown_remaining - delta_seconds);
        state.attack_wave_cooldown_remaining =
            std::max(0.0f, state.attack_wave_cooldown_remaining - delta_seconds);
        if (state.think_cooldown_remaining > 0.0f) {
            continue;
        }

        state.think_cooldown_remaining = std::max(state.profile.think_interval, 0.05f);
        // anchor acts like a rough base center for defense rallying and staging
        const glm::vec3 anchor = teamAnchor(team, frame);
        const RtsWorldUnitSnapshot* visible_enemy_unit =
            prioritizedEnemyUnit(team, anchor, std::numeric_limits<float>::max(), frame);
        const RtsWorldBuildingSnapshot* visible_enemy_building =
            prioritizedEnemyBuilding(team, anchor, frame);
        // remember the last observed enemy position so armies can keep pushing even after vision is lost
        if (visible_enemy_unit) {
            state.has_last_seen_enemy_position = true;
            state.last_seen_enemy_position = visible_enemy_unit->position;
        } else if (visible_enemy_building) {
            state.has_last_seen_enemy_position = true;
            state.last_seen_enemy_position = visible_enemy_building->center;
        }

        if (state.profile.auto_harvest &&
            !state.profile.worker_archetype_id.empty() &&
            !frame.resource_nodes.empty()) {
            // only idle workers get auto harvest jobs so existing player or ai orders are not overwritten
            const std::vector<std::uint32_t> idle_workers =
                idleUnitsForTeam(team, frame, state.profile.worker_archetype_id, true);
            for (const std::uint32_t worker_id : idle_workers) {
                const auto worker_it = std::find_if(
                    frame.units.begin(), frame.units.end(),
                    [worker_id](const RtsWorldUnitSnapshot& unit) { return unit.unit_id == worker_id; });
                if (worker_it == frame.units.end()) {
                    continue;
                }
                const std::uint32_t node_id = nearestResourceNode(worker_it->position, frame);
                if (node_id == 0) {
                    break;
                }
                // one harvest command is emitted per idle worker
                commands.push_back(RtsAiCommand{
                    RtsAiCommandType::issue_harvest,
                    team,
                    worker_id,
                    {},
                    0,
                    node_id
                });
            }
        }

        if (state.profile.auto_produce && !state.profile.production_priority.empty()) {
            // do a simple force composition estimate from current units plus already queued workers
            int worker_count = 0;
            int combat_count = 0;
            int enemy_worker_count = 0;
            int enemy_combat_count = 0;
            for (const RtsWorldUnitSnapshot& unit : frame.units) {
                if (unit.team == team) {
                    if (unit.archetype_id == state.profile.worker_archetype_id) {
                        ++worker_count;
                    } else {
                        ++combat_count;
                    }
                } else if (isWorkerArchetypeId(unit.archetype_id)) {
                    ++enemy_worker_count;
                } else {
                    ++enemy_combat_count;
                }
            }
            for (const RtsWorldProductionSnapshot& queue : frame.production) {
                if (queue.team != team) {
                    continue;
                }
                for (const RtsWorldProductionEntrySnapshot& queued : queue.queue) {
                    if (queued.unit_archetype_id == state.profile.worker_archetype_id) {
                        ++worker_count;
                    }
                }
            }

            std::vector<std::string> desired_units{};
            if (worker_count < state.profile.minimum_workers &&
                !state.profile.worker_archetype_id.empty()) {
                // first fill missing worker count if needed
                desired_units.push_back(state.profile.worker_archetype_id);
            }
            // ask for extra combat when the enemy fielded more combat units than we have
            // or when the enemy has lots of workers and we still have almost no escort force
            const bool need_more_combat =
                enemy_combat_count > combat_count ||
                (enemy_worker_count > enemy_combat_count && combat_count < std::max(2, worker_count));
            if (need_more_combat) {
                // then add the first preferred combat unit type
                for (const std::string& candidate : state.profile.production_priority) {
                    if (!candidate.empty() && candidate != state.profile.worker_archetype_id) {
                        desired_units.push_back(candidate);
                        break;
                    }
                }
            }
            desired_units.insert(desired_units.end(),
                                 state.profile.production_priority.begin(),
                                 state.profile.production_priority.end());

            // only buildings with empty queues receive a new production request here
            for (const RtsWorldProductionSnapshot& queue : frame.production) {
                if (queue.team != team || !queue.queue.empty()) {
                    continue;
                }
                for (const std::string& candidate : desired_units) {
                    if (candidate.empty()) {
                        continue;
                    }
                    commands.push_back(RtsAiCommand{
                        RtsAiCommandType::enqueue_production,
                        team,
                        0,
                        {},
                        queue.building_id,
                        0,
                        0,
                        glm::vec3(0.0f),
                        candidate
                    });
                    break;
                }
            }
        }

        if (state.profile.auto_attack) {
            // first decide whether we are defending at home or free to assemble an attack wave
            const RtsWorldUnitSnapshot* defending_target =
                prioritizedEnemyUnit(team, anchor, state.profile.defend_radius, frame);
            std::vector<std::uint32_t> available_combat_units =
                availableCombatUnitsForTeam(team, frame, state.profile.worker_archetype_id);
            const std::vector<std::uint32_t> total_combat_units =
                combatUnitsForTeam(team, frame, state.profile.worker_archetype_id);
            if (available_combat_units.empty()) {
                continue;
            }

            const std::vector<std::uint32_t> retreating_units =
                lowHealthRetreatUnits(team, frame, state.profile.worker_archetype_id);
            if (!retreating_units.empty()) {
                // individually weak front line units get ordered back toward the anchor
                commands.push_back(RtsAiCommand{
                    RtsAiCommandType::issue_move,
                    team,
                    0,
                    retreating_units,
                    0,
                    0,
                    0,
                    anchor
                });
                available_combat_units.erase(
                    std::remove_if(
                        available_combat_units.begin(),
                        available_combat_units.end(),
                        [&retreating_units](std::uint32_t unit_id) {
                            return std::find(retreating_units.begin(),
                                             retreating_units.end(),
                                             unit_id) != retreating_units.end();
                        }),
                    available_combat_units.end());
            }
            if (available_combat_units.empty()) {
                continue;
            }

            const float ally_strength = nearbyStrength(team, anchor, state.profile.defend_radius, frame, false);
            const float enemy_strength = nearbyStrength(team, anchor, state.profile.defend_radius, frame, true);
            // retreat if the local force estimate looks decisively unfavorable
            const bool should_retreat =
                enemy_strength > ally_strength * 1.35f && enemy_strength >= 1.6f;
            const int enemy_combat_count = static_cast<int>(std::count_if(
                frame.units.begin(), frame.units.end(),
                [team](const RtsWorldUnitSnapshot& unit) {
                    return unit.team != team && !contains_worker_token(unit.archetype_id);
                }));
            const int dynamic_attack_force =
                std::max(state.profile.attack_force_size, 1 + enemy_combat_count / 2);
            // dynamic_attack_force scales the attack threshold upward as the observed enemy army grows
            // that way the ai waits for a more reasonable force before committing across the map

            if (should_retreat) {
                commands.push_back(RtsAiCommand{
                    RtsAiCommandType::issue_move,
                    team,
                    0,
                    available_combat_units,
                    0,
                    0,
                    0,
                    anchor
                });
                continue;
            }

            if (defending_target) {
                // if enemies are near the base use all available combat units to defend immediately
                commands.push_back(RtsAiCommand{
                    RtsAiCommandType::issue_attack_move,
                    team,
                    0,
                    available_combat_units,
                    0,
                    0,
                    defending_target->unit_id,
                    defending_target->position
                });
                continue;
            }

            const RtsWorldUnitSnapshot* target_unit = visible_enemy_unit;
            const RtsWorldBuildingSnapshot* target_building = visible_enemy_building;
            const glm::vec3 objective =
                target_unit ? target_unit->position :
                (target_building ? target_building->center :
                 (state.has_last_seen_enemy_position ? state.last_seen_enemy_position : anchor));
            const bool has_objective =
                target_unit != nullptr || target_building != nullptr || state.has_last_seen_enemy_position;
            // objective resolution order matters
            // a live enemy unit is the most urgent tactical target
            // a visible building is next
            // and the remembered last seen position is the fallback when vision is lost

            if (static_cast<int>(total_combat_units.size()) < dynamic_attack_force || !has_objective) {
                if (!available_combat_units.empty()) {
                    // without a full attack force the ai probes with a scout or moves toward a scouting point
                    const std::optional<glm::vec3> scout_point = scoutingPoint(team, state, anchor, frame);
                    commands.push_back(RtsAiCommand{
                        RtsAiCommandType::issue_move,
                        team,
                        available_combat_units.front(),
                        {available_combat_units.front()},
                        0,
                        0,
                        0,
                        scout_point.has_value() ? scout_point.value() : objective
                    });
                }
                continue;
            }
            const float objective_distance = planar_distance(anchor, objective);
            if (state.attack_wave_cooldown_remaining > 0.0f) {
                // while wave cooldown is active gather nearer to the midpoint rather than full commit
                // this creates a rally phase so the group compresses before the next push
                commands.push_back(RtsAiCommand{
                    RtsAiCommandType::issue_move,
                    team,
                    0,
                    available_combat_units,
                    0,
                    0,
                    0,
                    midpointStagingPoint(anchor, objective, 0.35f, 2.0f, 5.0f)
                });
                continue;
            }
            if (static_cast<int>(available_combat_units.size()) >= dynamic_attack_force + 2 &&
                objective_distance >= 8.0f) {
                // large armies at long distance get split into left and right flank staging groups
                // the split is purely positional
                // even index units go left and odd index units go right
                std::vector<std::uint32_t> left_group{};
                std::vector<std::uint32_t> right_group{};
                left_group.reserve((available_combat_units.size() + 1) / 2);
                right_group.reserve(available_combat_units.size() / 2);
                for (std::size_t i = 0; i < available_combat_units.size(); ++i) {
                    if ((i % 2) == 0) {
                        left_group.push_back(available_combat_units[i]);
                    } else {
                        right_group.push_back(available_combat_units[i]);
                    }
                }
                if (!left_group.empty()) {
                    commands.push_back(RtsAiCommand{
                        RtsAiCommandType::issue_move,
                        team,
                        0,
                        left_group,
                        0,
                        0,
                        0,
                        flankPoint(anchor, objective, state.preferred_flank_sign)
                    });
                }
                if (!right_group.empty()) {
                    commands.push_back(RtsAiCommand{
                        RtsAiCommandType::issue_move,
                        team,
                        0,
                        right_group,
                        0,
                        0,
                        0,
                        flankPoint(anchor, objective, -state.preferred_flank_sign)
                    });
                }
                state.preferred_flank_sign *= -1.0f;
                state.attack_wave_cooldown_remaining = 1.2f;
                continue;
            }
            if (static_cast<int>(available_combat_units.size()) < dynamic_attack_force &&
                available_combat_units.size() >= 2) {
                // partially assembled armies still rally forward instead of idling at the base
                commands.push_back(RtsAiCommand{
                    RtsAiCommandType::issue_move,
                    team,
                    0,
                    available_combat_units,
                    0,
                    0,
                    0,
                    midpointStagingPoint(anchor, objective)
                });
                continue;
            }
            // otherwise launch or continue the actual attack move toward the best objective
            commands.push_back(RtsAiCommand{
                RtsAiCommandType::issue_attack_move,
                team,
                0,
                available_combat_units,
                0,
                0,
                target_unit ? target_unit->unit_id : 0,
                objective
            });
            state.attack_wave_cooldown_remaining = objective_distance > 6.0f ? 0.8f : 0.3f;
        }
    }

    return commands;
}

glm::vec3 RtsAiSystem::teamAnchor(int team, const RtsAiFrame& frame) const {
    // prefer averaging building centers because they are a better stable notion of base location
    glm::vec3 sum(0.0f);
    int count = 0;
    for (const RtsWorldBuildingSnapshot& building : frame.buildings) {
        if (building.team != team) {
            continue;
        }
        sum += building.center;
        ++count;
    }
    if (count == 0) {
        // if no buildings survive then use the average unit position as the fallback anchor
        for (const RtsWorldUnitSnapshot& unit : frame.units) {
            if (unit.team != team) {
                continue;
            }
            sum += unit.position;
            ++count;
        }
    }
    return count > 0 ? sum / static_cast<float>(count) : glm::vec3(0.0f);
}

bool RtsAiSystem::isWorkerArchetypeId(const std::string& archetype_id) {
    return contains_worker_token(archetype_id);
}

float RtsAiSystem::unitStrength(const RtsWorldUnitSnapshot& unit) {
    // this is intentionally rough not a simulation
    // workers count less and damaged units count less
    const float health_ratio = healthRatio(unit);
    const float base = isWorkerArchetypeId(unit.archetype_id) ? 0.7f : 1.4f;
    return base * health_ratio;
}

float RtsAiSystem::healthRatio(const RtsWorldUnitSnapshot& unit) {
    // defend against divide by zero if a malformed snapshot appears
    return unit.max_health > 0.0f ? std::clamp(unit.health / unit.max_health, 0.0f, 1.0f) : 0.0f;
}

glm::vec3 RtsAiSystem::midpointStagingPoint(const glm::vec3& start,
                                            const glm::vec3& target,
                                            float distance_scale,
                                            float min_distance,
                                            float max_distance) {
    glm::vec2 delta(target.x - start.x, target.z - start.z);
    const float distance = glm::length(delta);
    if (distance <= 0.0001f) {
        return start;
    }
    // clamp the staging step so rally points stay neither too close nor too far
    const glm::vec2 direction = delta / distance;
    const float step = std::clamp(distance * distance_scale, min_distance, max_distance);
    return glm::vec3(start.x + direction.x * step, 0.0f, start.z + direction.y * step);
}

glm::vec3 RtsAiSystem::flankPoint(const glm::vec3& start,
                                  const glm::vec3& target,
                                  float flank_sign,
                                  float forward_scale,
                                  float flank_distance) {
    glm::vec2 delta(target.x - start.x, target.z - start.z);
    const float distance = glm::length(delta);
    if (distance <= 0.0001f) {
        return target;
    }
    const glm::vec2 forward = delta / distance;
    // side is the perpendicular vector in ground space used to offset the flank left or right
    const glm::vec2 side(-forward.y, forward.x);
    const float forward_distance = std::max(distance * forward_scale, 3.5f);
    const glm::vec2 staged = glm::vec2(start.x, start.z) +
                             forward * forward_distance +
                             side * flank_distance * flank_sign;
    return glm::vec3(staged.x, 0.0f, staged.y);
}

std::vector<std::uint32_t> RtsAiSystem::idleUnitsForTeam(int team,
                                                         const RtsAiFrame& frame,
                                                         const std::string& archetype_id,
                                                         bool match_archetype) const {
    std::vector<std::uint32_t> unit_ids{};
    for (const RtsWorldUnitSnapshot& unit : frame.units) {
        // idle means no active order and not holding position
        if (unit.team != team || unit.holding_position || unit.active_order.has_value()) {
            continue;
        }
        const bool archetype_matches = unit.archetype_id == archetype_id;
        if (match_archetype != archetype_matches) {
            continue;
        }
        unit_ids.push_back(unit.unit_id);
    }
    return unit_ids;
}

std::vector<std::uint32_t> RtsAiSystem::availableCombatUnitsForTeam(
    int team,
    const RtsAiFrame& frame,
    const std::string& worker_archetype_id) const {
    std::vector<std::uint32_t> unit_ids{};
    for (const RtsWorldUnitSnapshot& unit : frame.units) {
        // available means combat capable and currently not locked into another order
        if (unit.team != team ||
            unit.holding_position ||
            unit.active_order.has_value() ||
            (!worker_archetype_id.empty() && unit.archetype_id == worker_archetype_id)) {
            continue;
        }
        unit_ids.push_back(unit.unit_id);
    }
    return unit_ids;
}

std::vector<std::uint32_t> RtsAiSystem::combatUnitsForTeam(
    int team,
    const RtsAiFrame& frame,
    const std::string& worker_archetype_id) const {
    std::vector<std::uint32_t> unit_ids{};
    for (const RtsWorldUnitSnapshot& unit : frame.units) {
        // unlike availableCombatUnitsForTeam this counts busy combat units too
        if (unit.team != team ||
            (!worker_archetype_id.empty() && unit.archetype_id == worker_archetype_id)) {
            continue;
        }
        unit_ids.push_back(unit.unit_id);
    }
    return unit_ids;
}

float RtsAiSystem::nearbyStrength(int team,
                                  const glm::vec3& origin,
                                  float radius,
                                  const RtsAiFrame& frame,
                                  bool enemies_only) const {
    float total = 0.0f;
    for (const RtsWorldUnitSnapshot& unit : frame.units) {
        if (unit.health <= 0.0f) {
            continue;
        }
        // enemies_only flips whether we are summing allies around the origin or enemies around the origin
        if (enemies_only ? (unit.team == team) : (unit.team != team)) {
            continue;
        }
        if (planar_distance(origin, unit.position) > radius) {
            continue;
        }
        total += unitStrength(unit);
    }
    return total;
}

std::vector<std::uint32_t> RtsAiSystem::lowHealthRetreatUnits(
    int team,
    const RtsAiFrame& frame,
    const std::string& worker_archetype_id) const {
    std::vector<std::uint32_t> unit_ids{};
    for (const RtsWorldUnitSnapshot& unit : frame.units) {
        if (unit.team != team ||
            unit.active_order.has_value() ||
            unit.holding_position ||
            (!worker_archetype_id.empty() && unit.archetype_id == worker_archetype_id)) {
            continue;
        }
        const float ratio = healthRatio(unit);
        const float local_ally_strength = nearbyStrength(team, unit.position, 4.5f, frame, false);
        const float local_enemy_strength = nearbyStrength(team, unit.position, 4.5f, frame, true);
        // retreat if critically damaged
        // or if moderately damaged while the local fight is leaning against us
        if (ratio < 0.3f ||
            (ratio < 0.55f && local_enemy_strength > local_ally_strength * 1.15f)) {
            unit_ids.push_back(unit.unit_id);
        }
    }
    return unit_ids;
}

std::uint32_t RtsAiSystem::nearestResourceNode(const glm::vec3& origin, const RtsAiFrame& frame) const {
    std::uint32_t nearest_id = 0;
    float nearest_distance = std::numeric_limits<float>::max();
    for (const RtsWorldResourceNodeSnapshot& node : frame.resource_nodes) {
        if (node.remaining_amount <= 0) {
            continue;
        }
        // this is nearest by straight line distance only
        // actual movement feasibility is left to world pathing when the order executes
        const float distance = planar_distance(origin, node.center);
        if (distance < nearest_distance) {
            nearest_distance = distance;
            nearest_id = node.node_id;
        }
    }
    return nearest_id;
}

std::optional<glm::vec3> RtsAiSystem::scoutingPoint(int team,
                                                    TeamState& state,
                                                    const glm::vec3& anchor,
                                                    const RtsAiFrame& frame) const {
    std::vector<glm::vec3> candidates{};
    if (state.has_last_seen_enemy_position) {
        // remembered enemy positions get first priority
        candidates.push_back(state.last_seen_enemy_position);
    }

    std::vector<const RtsWorldBuildingSnapshot*> enemy_buildings{};
    enemy_buildings.reserve(frame.buildings.size());
    for (const RtsWorldBuildingSnapshot& building : frame.buildings) {
        if (building.team != team) {
            enemy_buildings.push_back(&building);
        }
    }
    std::sort(enemy_buildings.begin(), enemy_buildings.end(),
              [&anchor](const RtsWorldBuildingSnapshot* lhs, const RtsWorldBuildingSnapshot* rhs) {
                  // counts_for_victory buildings are scored higher than ordinary structures
                  const float lhs_score =
                      (lhs->counts_for_victory ? 100.0f : 0.0f) - planar_distance(anchor, lhs->center);
                  const float rhs_score =
                      (rhs->counts_for_victory ? 100.0f : 0.0f) - planar_distance(anchor, rhs->center);
                  return lhs_score > rhs_score;
              });
    for (const RtsWorldBuildingSnapshot* building : enemy_buildings) {
        candidates.push_back(midpointStagingPoint(anchor, building->center, 0.7f, 5.0f, 10.0f));
    }

    std::vector<const RtsWorldResourceNodeSnapshot*> nodes{};
    nodes.reserve(frame.resource_nodes.size());
    for (const RtsWorldResourceNodeSnapshot& node : frame.resource_nodes) {
        nodes.push_back(&node);
    }
    std::sort(nodes.begin(), nodes.end(),
              [&anchor](const RtsWorldResourceNodeSnapshot* lhs, const RtsWorldResourceNodeSnapshot* rhs) {
                  // distant rich nodes make decent scouting locations because bases often grow near them
                  const float lhs_score =
                      planar_distance(anchor, lhs->center) + static_cast<float>(lhs->remaining_amount) * 0.01f;
                  const float rhs_score =
                      planar_distance(anchor, rhs->center) + static_cast<float>(rhs->remaining_amount) * 0.01f;
                  return lhs_score > rhs_score;
              });
    for (const RtsWorldResourceNodeSnapshot* node : nodes) {
        candidates.push_back(node->center);
    }

    if (candidates.empty()) {
        return std::nullopt;
    }
    // rotate through the candidate list over time instead of always choosing the first point
    const std::size_t index = state.scout_target_index % candidates.size();
    state.scout_target_index = (state.scout_target_index + 1) % candidates.size();
    return candidates[index];
}

const RtsWorldBuildingSnapshot* RtsAiSystem::nearestEnemyBuilding(int team,
                                                                  const glm::vec3& origin,
                                                                  const RtsAiFrame& frame) const {
    const RtsWorldBuildingSnapshot* nearest = nullptr;
    float nearest_distance = std::numeric_limits<float>::max();
    for (const RtsWorldBuildingSnapshot& building : frame.buildings) {
        if (building.team == team) {
            continue;
        }
        const float distance = planar_distance(origin, building.center);
        if (distance < nearest_distance) {
            // nearestEnemyBuilding is a pure geometric helper with no strategic weighting
            nearest_distance = distance;
            nearest = &building;
        }
    }
    return nearest;
}

const RtsWorldBuildingSnapshot* RtsAiSystem::prioritizedEnemyBuilding(
    int team,
    const glm::vec3& origin,
    const RtsAiFrame& frame) const {
    const RtsWorldBuildingSnapshot* best = nullptr;
    float best_score = std::numeric_limits<float>::lowest();
    for (const RtsWorldBuildingSnapshot& building : frame.buildings) {
        if (building.team == team) {
            continue;
        }
        // simple heuristic prefers victory critical buildings then towers then proximity
        float score = building.counts_for_victory ? 130.0f : 90.0f;
        if (building.registers_tower) {
            score += 15.0f;
        }
        score -= planar_distance(origin, building.center) * 3.0f;
        if (score > best_score) {
            best_score = score;
            best = &building;
        }
    }
    return best;
}

const RtsWorldUnitSnapshot* RtsAiSystem::nearestEnemyUnit(int team,
                                                          const glm::vec3& origin,
                                                          float radius,
                                                          const RtsAiFrame& frame) const {
    const RtsWorldUnitSnapshot* nearest = nullptr;
    float nearest_distance = radius;
    for (const RtsWorldUnitSnapshot& unit : frame.units) {
        if (unit.team == team || unit.health <= 0.0f) {
            continue;
        }
        const float distance = planar_distance(origin, unit.position);
        if (distance <= nearest_distance) {
            nearest_distance = distance;
            nearest = &unit;
        }
    }
    return nearest;
}

const RtsWorldUnitSnapshot* RtsAiSystem::prioritizedEnemyUnit(
    int team,
    const glm::vec3& origin,
    float radius,
    const RtsAiFrame& frame) const {
    const RtsWorldUnitSnapshot* best = nullptr;
    float best_score = std::numeric_limits<float>::lowest();
    for (const RtsWorldUnitSnapshot& unit : frame.units) {
        if (unit.team == team || unit.health <= 0.0f) {
            continue;
        }
        const float distance = planar_distance(origin, unit.position);
        if (distance > radius) {
            continue;
        }
        // prioritize combat units over workers then add a bonus for already damaged targets
        float score = isWorkerArchetypeId(unit.archetype_id) ? 70.0f : 115.0f;
        score += (1.0f - std::clamp(unit.health / std::max(unit.max_health, 0.001f), 0.0f, 1.0f)) * 20.0f;
        score -= distance * 4.0f;
        if (score > best_score) {
            best_score = score;
            best = &unit;
        }
    }
    return best;
}
