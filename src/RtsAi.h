/**
 * @file RtsAi.h
 * @brief simple team level ai director that turns snapshots into commands
 */
#ifndef RTS_AI_H
#define RTS_AI_H

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

#include "RtsTypes.h"

enum class RtsAiCommandType {
    issue_harvest,
    issue_move,
    issue_attack_move,
    enqueue_production
};

/**
 * @brief one command emitted by the ai for the world to execute
 *
 * this is intentionally high level
 * the ai does not mutate the world directly
 * it just asks for moves harvests attack moves or production
 */
struct RtsAiCommand {
    RtsAiCommandType type = RtsAiCommandType::issue_attack_move;
    int team = -1;
    std::uint32_t unit_id = 0;
    std::vector<std::uint32_t> unit_ids{};
    std::uint32_t building_id = 0;
    std::uint32_t resource_node_id = 0;
    std::uint32_t target_unit_id = 0;
    glm::vec3 target_position = glm::vec3(0.0f);
    std::string archetype_id{};
};

/**
 * @brief lightweight world snapshot passed into the ai each think step
 *
 * the ai only needs readonly data
 * so it works from copied snapshots instead of direct world pointers
 */
struct RtsAiFrame {
    std::vector<RtsWorldUnitSnapshot> units{};
    std::vector<RtsWorldBuildingSnapshot> buildings{};
    std::vector<RtsWorldProductionSnapshot> production{};
    std::vector<RtsWorldResourceNodeSnapshot> resource_nodes{};
    glm::vec2 world_min_xz = glm::vec2(0.0f);
    glm::vec2 world_max_xz = glm::vec2(0.0f);
    // optional per team visibility filters so the ai can reason under the same fog rules as players
    std::function<bool(int, const RtsWorldUnitSnapshot&)> unit_visibility_for_team{};
    std::function<bool(int, const RtsWorldBuildingSnapshot&)> building_visibility_for_team{};
    std::function<bool(int, const RtsWorldResourceNodeSnapshot&)> resource_visibility_for_team{};
};

/**
 * @brief per team ai system for harvesting production scouting and attacks
 *
 * each team gets a TeamState plus a behavior profile
 * update examines the current frame and returns a list of desired commands
 */
class RtsAiSystem {
public:
    // store or replace one teams behavior profile
    void setTeamProfile(int team, const RtsAiProfile& profile);
    // remove one teams ai state entirely
    bool removeTeamProfile(int team);
    // quick lookup helpers for callers
    bool hasTeamProfile(int team) const;
    const RtsAiProfile* teamProfile(int team) const;

    /**
     * @brief runs one ai think step for every configured team
     * @param delta_seconds frame delta
     * @param frame readonly snapshot of the current world
     * @return list of commands the world may apply
     */
    std::vector<RtsAiCommand> update(float delta_seconds, const RtsAiFrame& frame);

private:
    /**
     * @brief persistent ai memory for one team between updates
     */
    struct TeamState {
        struct RecentGroupCommand {
            RtsAiCommandType type = RtsAiCommandType::issue_move;
            std::vector<std::uint32_t> unit_ids{};
            std::uint32_t target_unit_id = 0;
            glm::vec3 target_position = glm::vec3(0.0f);
            float cooldown_remaining = 0.0f;
        };

        // current behavior knobs for this team
        RtsAiProfile profile{};
        // ai only thinks every so often instead of every frame
        float think_cooldown_remaining = 0.0f;
        // spacing timer between major attack waves
        float attack_wave_cooldown_remaining = 0.0f;
        // rotating index for scouting candidate points
        std::size_t scout_target_index = 0;
        // used to alternate left and right flank staging
        float preferred_flank_sign = 1.0f;
        // remembered objective when no enemy is currently visible
        bool has_last_seen_enemy_position = false;
        glm::vec3 last_seen_enemy_position = glm::vec3(0.0f);
        // scouting keeps one target for a short time so units do not jitter between many possibilities
        bool has_scout_target = false;
        glm::vec3 scout_target = glm::vec3(0.0f);
        float scout_target_cooldown_remaining = 0.0f;
        // recently issued formation style commands used to suppress identical reissues
        std::vector<RecentGroupCommand> recent_group_commands{};
    };

    // approximate team center used for defense and staging
    glm::vec3 teamAnchor(int team, const RtsAiFrame& frame) const;
    // string heuristic for worker style units
    static bool isWorkerArchetypeId(const std::string& archetype_id);
    // very rough combat valuation helpers
    static float unitStrength(const RtsWorldUnitSnapshot& unit);
    static float healthRatio(const RtsWorldUnitSnapshot& unit);
    // helper for gathering units at a point between base and objective
    static glm::vec3 midpointStagingPoint(const glm::vec3& start,
                                          const glm::vec3& target,
                                          float distance_scale = 0.45f,
                                          float min_distance = 2.5f,
                                          float max_distance = 7.0f);
    // helper for splitting groups onto a side approach
    static glm::vec3 flankPoint(const glm::vec3& start,
                                const glm::vec3& target,
                                float flank_sign,
                                float forward_scale = 0.55f,
                                float flank_distance = 3.0f);
    // unit filters used by the main update logic
    std::vector<std::uint32_t> idleUnitsForTeam(int team,
                                                const RtsAiFrame& frame,
                                                const std::string& archetype_id,
                                                bool match_archetype) const;
    std::vector<std::uint32_t> availableCombatUnitsForTeam(int team,
                                                           const RtsAiFrame& frame,
                                                           const std::string& worker_archetype_id) const;
    std::vector<std::uint32_t> combatUnitsForTeam(int team,
                                                  const RtsAiFrame& frame,
                                                  const std::string& worker_archetype_id) const;
    float nearbyStrength(int team,
                         const glm::vec3& origin,
                         float radius,
                         const RtsAiFrame& frame,
                         bool enemies_only) const;
    std::vector<std::uint32_t> lowHealthRetreatUnits(int team,
                                                     const RtsAiFrame& frame,
                                                     const std::string& worker_archetype_id) const;
    std::uint32_t nearestResourceNode(int team,
                                      const glm::vec3& origin,
                                      const RtsAiFrame& frame) const;
    std::optional<glm::vec3> scoutingPoint(int team,
                                           TeamState& state,
                                           const glm::vec3& anchor,
                                           const RtsAiFrame& frame) const;
    const RtsWorldBuildingSnapshot* nearestEnemyBuilding(int team,
                                                         const glm::vec3& origin,
                                                         const RtsAiFrame& frame) const;
    const RtsWorldBuildingSnapshot* prioritizedEnemyBuilding(int team,
                                                             const glm::vec3& origin,
                                                             const RtsAiFrame& frame) const;
    const RtsWorldUnitSnapshot* nearestEnemyUnit(int team,
                                                 const glm::vec3& origin,
                                                 float radius,
                                                 const RtsAiFrame& frame) const;
    const RtsWorldUnitSnapshot* prioritizedEnemyUnit(int team,
                                                     const glm::vec3& origin,
                                                    float radius,
                                                    const RtsAiFrame& frame) const;

    // stored ai memory for each team that currently has a profile
    std::unordered_map<int, TeamState> teams_;
};

#endif
