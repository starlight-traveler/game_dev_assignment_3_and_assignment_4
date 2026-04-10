/**
 * @file RtsEconomy.h
 * @brief team resources and harvestable resource node state
 */
#ifndef RTS_ECONOMY_H
#define RTS_ECONOMY_H

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "BuildingSystem.h"
#include "RtsTypes.h"
#include "TerrainGrid.h"

/**
 * @brief owns team stockpiles and finite resource nodes
 */
class RtsEconomySystem {
public:
    /**
     * @brief one resource patch on the map
     *
     * this is the mutable live version of a resource node
     * the world uses it internally
     * then exports lighter snapshot data for ui ai and tests
     */
    struct ResourceNodeState {
        // stable id used by worker orders and lookups
        std::uint32_t node_id;
        // resource type this node yields
        std::string resource_id;
        // terrain cell that owns the node
        GridCoord cell;
        // how much resource is still left before the node disappears
        int remaining_amount;
    };

    RtsEconomySystem();

    // team resource balance helpers
    void setTeamResourceAmount(int team, const std::string& resource_id, int amount);
    int addTeamResourceAmount(int team, const std::string& resource_id, int delta);
    int teamResourceAmount(int team, const std::string& resource_id) const;
    std::vector<RtsTeamResourceSnapshot> teamResourceSnapshots(int team) const;

    // cost spending helpers used by production and construction
    bool canAffordCosts(int team, const std::vector<RtsResourceCost>& costs) const;
    bool spendTeamResources(int team, const std::vector<RtsResourceCost>& costs);
    void refundTeamResources(int team, const std::vector<RtsResourceCost>& costs);

    // map resource node helpers
    std::optional<std::uint32_t> addResourceNode(const TerrainGrid& terrain,
                                                 const BuildingSystem& buildings,
                                                 const std::string& resource_id,
                                                 const GridCoord& cell,
                                                 int amount);
    bool removeResourceNode(std::uint32_t node_id);
    int resourceNodeAmount(std::uint32_t node_id) const;
    int harvestResourceNode(std::uint32_t node_id, int requested_amount);
    const ResourceNodeState* findResourceNode(std::uint32_t node_id) const;
    std::vector<RtsWorldResourceNodeSnapshot> resourceNodeSnapshots(const TerrainGrid& terrain) const;

private:
    // per team per resource stockpile values
    std::unordered_map<int, std::unordered_map<std::string, int>> team_resources_;
    // all currently active map resource nodes keyed by node id
    std::unordered_map<std::uint32_t, ResourceNodeState> resource_nodes_;
    // monotonically increasing id generator for new nodes
    std::uint32_t next_resource_node_id_;
};

#endif
