/**
 * @file RtsProduction.h
 * @brief production queues for buildings that spawn units over time
 */
#ifndef RTS_PRODUCTION_H
#define RTS_PRODUCTION_H

#include <cstdint>
#include <deque>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

#include "RtsTypes.h"

/**
 * @brief keeps one queue per producing building
 *
 * the world decides what can be queued and what units mean
 * this class only tracks time and queue order
 */
class RtsProductionSystem {
public:
    /**
     * @brief one queued unit still waiting to finish
     */
    struct ProductionEntry {
        std::string unit_archetype_id;
        float remaining_time;
    };

    /**
     * @brief queue and rally point for one building
     */
    struct BuildingProductionState {
        glm::vec3 rally_point;
        std::deque<ProductionEntry> production_queue;
    };

    // register or remove buildings that are allowed to produce
    void registerBuilding(std::uint32_t building_id, const glm::vec3& rally_point);
    void unregisterBuilding(std::uint32_t building_id);
    bool hasBuilding(std::uint32_t building_id) const;

    // rally point accessors
    bool setRallyPoint(std::uint32_t building_id, const glm::vec3& rally_point);
    glm::vec3 rallyPoint(std::uint32_t building_id) const;

    // queue editing helpers
    bool enqueueProduction(std::uint32_t building_id,
                           const std::string& unit_archetype_id,
                           float production_time);
    std::optional<ProductionEntry> cancelLastProduction(std::uint32_t building_id);
    std::vector<ProductionEntry> clearProductionQueue(std::uint32_t building_id);

    template <typename TeamLookup>
    int queuedSupplyForTeam(int team,
                            const TeamLookup& team_lookup,
                            const std::function<int(const std::string&)>& unit_supply_lookup) const {
        // this counts supply that is still only in queues so the world can reserve future cap usage
        int total = 0;
        for (const auto& entry : buildings_) {
            if (team_lookup(entry.first) != team) {
                continue;
            }
            for (const ProductionEntry& queued : entry.second.production_queue) {
                total += unit_supply_lookup(queued.unit_archetype_id);
            }
        }
        return total;
    }

    void update(float delta_seconds,
                const std::function<bool(std::uint32_t)>& is_building_valid,
                const std::function<bool(std::uint32_t, const std::string&, const glm::vec3&)>&
                    try_spawn_unit);

    // snapshot export used by ui ai and tests
    std::vector<RtsWorldProductionSnapshot> snapshots(
        const std::function<int(std::uint32_t)>& team_lookup,
        const std::function<std::string(std::uint32_t)>& archetype_lookup) const;

private:
    // one production state per registered building
    std::unordered_map<std::uint32_t, BuildingProductionState> buildings_;
};

#endif
