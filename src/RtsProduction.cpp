#include "RtsProduction.h"

#include <algorithm>

void RtsProductionSystem::registerBuilding(std::uint32_t building_id, const glm::vec3& rally_point) {
    // registering overwrites old state which is useful when rebuilding a buildings runtime state
    buildings_[building_id] = BuildingProductionState{rally_point, {}};
}

void RtsProductionSystem::unregisterBuilding(std::uint32_t building_id) {
    buildings_.erase(building_id);
}

bool RtsProductionSystem::hasBuilding(std::uint32_t building_id) const {
    return buildings_.find(building_id) != buildings_.end();
}

bool RtsProductionSystem::setRallyPoint(std::uint32_t building_id, const glm::vec3& rally_point) {
    auto it = buildings_.find(building_id);
    if (it == buildings_.end()) {
        return false;
    }
    it->second.rally_point = rally_point;
    return true;
}

glm::vec3 RtsProductionSystem::rallyPoint(std::uint32_t building_id) const {
    const auto it = buildings_.find(building_id);
    return it == buildings_.end() ? glm::vec3(0.0f) : it->second.rally_point;
}

bool RtsProductionSystem::enqueueProduction(std::uint32_t building_id,
                                            const std::string& unit_archetype_id,
                                            float production_time) {
    auto it = buildings_.find(building_id);
    if (it == buildings_.end() || unit_archetype_id.empty() || production_time <= 0.0f) {
        return false;
    }
    // queue order matters because only the front item ticks down each frame
    it->second.production_queue.push_back(ProductionEntry{unit_archetype_id, production_time});
    return true;
}

std::optional<RtsProductionSystem::ProductionEntry> RtsProductionSystem::cancelLastProduction(
    std::uint32_t building_id) {
    auto it = buildings_.find(building_id);
    if (it == buildings_.end() || it->second.production_queue.empty()) {
        return std::nullopt;
    }

    ProductionEntry entry = it->second.production_queue.back();
    it->second.production_queue.pop_back();
    return entry;
}

std::vector<RtsProductionSystem::ProductionEntry> RtsProductionSystem::clearProductionQueue(
    std::uint32_t building_id) {
    std::vector<ProductionEntry> cleared{};
    auto it = buildings_.find(building_id);
    if (it == buildings_.end()) {
        return cleared;
    }

    cleared.assign(it->second.production_queue.begin(), it->second.production_queue.end());
    it->second.production_queue.clear();
    return cleared;
}

void RtsProductionSystem::update(
    float delta_seconds,
    const std::function<bool(std::uint32_t)>& is_building_valid,
    const std::function<bool(std::uint32_t, const std::string&, const glm::vec3&)>& try_spawn_unit) {
    for (auto& entry : buildings_) {
        const std::uint32_t building_id = entry.first;
        BuildingProductionState& state = entry.second;
        if (!is_building_valid(building_id) || state.production_queue.empty()) {
            continue;
        }

        // only the queue front is actively being produced
        ProductionEntry& current = state.production_queue.front();
        current.remaining_time = std::max(0.0f, current.remaining_time - delta_seconds);
        if (current.remaining_time > 0.0f) {
            continue;
        }

        // pop only when the world successfully spawned the completed unit
        if (try_spawn_unit(building_id, current.unit_archetype_id, state.rally_point)) {
            state.production_queue.pop_front();
        }
    }
}

std::vector<RtsWorldProductionSnapshot> RtsProductionSystem::snapshots(
    const std::function<int(std::uint32_t)>& team_lookup,
    const std::function<std::string(std::uint32_t)>& archetype_lookup) const {
    std::vector<RtsWorldProductionSnapshot> snapshots{};
    snapshots.reserve(buildings_.size());
    for (const auto& entry : buildings_) {
        std::vector<RtsWorldProductionEntrySnapshot> queue{};
        queue.reserve(entry.second.production_queue.size());
        for (std::size_t i = 0; i < entry.second.production_queue.size(); ++i) {
            const ProductionEntry& production = entry.second.production_queue[i];
            queue.push_back(RtsWorldProductionEntrySnapshot{
                production.unit_archetype_id,
                production.remaining_time,
                i == 0
            });
        }
        snapshots.push_back(RtsWorldProductionSnapshot{
            entry.first,
            team_lookup(entry.first),
            archetype_lookup(entry.first),
            entry.second.rally_point,
            queue
        });
    }
    std::sort(snapshots.begin(), snapshots.end(),
              [](const RtsWorldProductionSnapshot& lhs, const RtsWorldProductionSnapshot& rhs) {
                  return lhs.building_id < rhs.building_id;
              });
    return snapshots;
}
