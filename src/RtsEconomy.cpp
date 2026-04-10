#include "RtsEconomy.h"

#include <algorithm>

RtsEconomySystem::RtsEconomySystem()
    // ids start at one so zero can stay reserved as an invalid no node sentinel
    : team_resources_(),
      resource_nodes_(),
      next_resource_node_id_(1) {}

void RtsEconomySystem::setTeamResourceAmount(int team,
                                             const std::string& resource_id,
                                             int amount) {
    if (resource_id.empty()) {
        return;
    }
    // balances are clamped to zero so callers never observe negative stockpiles
    team_resources_[team][resource_id] = std::max(amount, 0);
}

int RtsEconomySystem::addTeamResourceAmount(int team,
                                            const std::string& resource_id,
                                            int delta) {
    if (resource_id.empty()) {
        return 0;
    }
    // add the signed delta then clamp back to zero
    int& stored = team_resources_[team][resource_id];
    stored = std::max(stored + delta, 0);
    return stored;
}

int RtsEconomySystem::teamResourceAmount(int team, const std::string& resource_id) const {
    const auto team_it = team_resources_.find(team);
    if (team_it == team_resources_.end()) {
        // missing team entries just mean that team has never touched this resource yet
        return 0;
    }
    const auto resource_it = team_it->second.find(resource_id);
    return resource_it == team_it->second.end() ? 0 : resource_it->second;
}

std::vector<RtsTeamResourceSnapshot> RtsEconomySystem::teamResourceSnapshots(int team) const {
    std::vector<RtsTeamResourceSnapshot> snapshots{};
    const auto team_it = team_resources_.find(team);
    if (team_it == team_resources_.end()) {
        return snapshots;
    }

    // sort snapshots for deterministic ui and test output
    snapshots.reserve(team_it->second.size());
    for (const auto& entry : team_it->second) {
        snapshots.push_back(RtsTeamResourceSnapshot{entry.first, entry.second});
    }
    std::sort(snapshots.begin(), snapshots.end(),
              [](const RtsTeamResourceSnapshot& lhs, const RtsTeamResourceSnapshot& rhs) {
                  return lhs.resource_id < rhs.resource_id;
              });
    return snapshots;
}

bool RtsEconomySystem::canAffordCosts(int team, const std::vector<RtsResourceCost>& costs) const {
    for (const RtsResourceCost& cost : costs) {
        if (cost.amount <= 0) {
            // zero or negative costs are ignored so helper code can pass mixed lists safely
            continue;
        }
        if (cost.resource_id.empty() || teamResourceAmount(team, cost.resource_id) < cost.amount) {
            return false;
        }
    }
    return true;
}

bool RtsEconomySystem::spendTeamResources(int team, const std::vector<RtsResourceCost>& costs) {
    if (!canAffordCosts(team, costs)) {
        return false;
    }
    // only mutate balances after the whole affordability check passes
    for (const RtsResourceCost& cost : costs) {
        if (cost.amount > 0) {
            addTeamResourceAmount(team, cost.resource_id, -cost.amount);
        }
    }
    return true;
}

void RtsEconomySystem::refundTeamResources(int team, const std::vector<RtsResourceCost>& costs) {
    for (const RtsResourceCost& cost : costs) {
        // refunds are intentionally permissive because cancel paths should be easy to call safely
        if (cost.amount > 0 && !cost.resource_id.empty()) {
            addTeamResourceAmount(team, cost.resource_id, cost.amount);
        }
    }
}

std::optional<std::uint32_t> RtsEconomySystem::addResourceNode(const TerrainGrid& terrain,
                                                               const BuildingSystem& buildings,
                                                               const std::string& resource_id,
                                                               const GridCoord& cell,
                                                               int amount) {
    if (resource_id.empty() ||
        amount <= 0 ||
        !terrain.isValidCell(cell) ||
        buildings.isCellOccupied(cell)) {
        return std::nullopt;
    }

    // do not allow two nodes in the same cell
    for (const auto& entry : resource_nodes_) {
        if (entry.second.cell.x == cell.x && entry.second.cell.y == cell.y) {
            return std::nullopt;
        }
    }

    const std::uint32_t node_id = next_resource_node_id_++;
    // store the live mutable node record that workers will harvest from over time
    resource_nodes_[node_id] = ResourceNodeState{
        node_id,
        resource_id,
        cell,
        amount
    };
    return node_id;
}

bool RtsEconomySystem::removeResourceNode(std::uint32_t node_id) {
    return resource_nodes_.erase(node_id) > 0;
}

int RtsEconomySystem::resourceNodeAmount(std::uint32_t node_id) const {
    const auto it = resource_nodes_.find(node_id);
    return it == resource_nodes_.end() ? 0 : it->second.remaining_amount;
}

int RtsEconomySystem::harvestResourceNode(std::uint32_t node_id, int requested_amount) {
    if (requested_amount <= 0) {
        return 0;
    }

    auto it = resource_nodes_.find(node_id);
    if (it == resource_nodes_.end()) {
        return 0;
    }

    // harvest no more than what remains in the node
    const int harvested = std::min(requested_amount, it->second.remaining_amount);
    it->second.remaining_amount -= harvested;
    if (it->second.remaining_amount <= 0) {
        // depleted nodes are removed immediately so future lookups clearly fail
        resource_nodes_.erase(it);
    }
    return harvested;
}

const RtsEconomySystem::ResourceNodeState* RtsEconomySystem::findResourceNode(
    std::uint32_t node_id) const {
    const auto it = resource_nodes_.find(node_id);
    return it == resource_nodes_.end() ? nullptr : &it->second;
}

std::vector<RtsWorldResourceNodeSnapshot> RtsEconomySystem::resourceNodeSnapshots(
    const TerrainGrid& terrain) const {
    std::vector<RtsWorldResourceNodeSnapshot> snapshots{};
    snapshots.reserve(resource_nodes_.size());
    for (const auto& entry : resource_nodes_) {
        const ResourceNodeState& node = entry.second;
        // cellCenter gives the world position used by workers and render code
        // snapshots hide the mutable map internals and expose only readonly data
        snapshots.push_back(RtsWorldResourceNodeSnapshot{
            node.node_id,
            node.resource_id,
            node.cell,
            terrain.cellCenter(node.cell),
            node.remaining_amount
        });
    }
    std::sort(snapshots.begin(), snapshots.end(),
              [](const RtsWorldResourceNodeSnapshot& lhs, const RtsWorldResourceNodeSnapshot& rhs) {
                  return lhs.node_id < rhs.node_id;
              });
    return snapshots;
}
