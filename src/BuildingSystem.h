/**
 * @file BuildingSystem.h
 * @brief building placement rules and occupancy tracking on top of the terrain grid
 */
#ifndef BUILDING_SYSTEM_H
#define BUILDING_SYSTEM_H

#include <cstddef>
#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

#include "TerrainGrid.h"

/**
 * @brief footprint and placement rules for one building type
 */
struct BuildingDefinition {
    int footprint_width;
    int footprint_height;
    bool blocks_movement;
    bool requires_buildable_ground;
};

/**
 * @brief one placed building instance stored by the occupancy system
 */
struct BuildingInstance {
    std::uint32_t id;
    GridCoord anchor;
    int footprint_width;
    int footprint_height;
    bool blocks_movement;
};

/**
 * @brief keeps track of which cells are occupied by which building
 *
 * this class only handles placement and occupancy bookkeeping
 * health team ownership and combat live in RtsWorld instead
 */
class BuildingSystem {
public:
    /**
     * @brief Constructs an empty building system sized to one terrain grid
     * @param grid_width Grid width in cells
     * @param grid_height Grid height in cells
     */
    BuildingSystem(int grid_width, int grid_height);

    /**
     * @brief Resizes the occupancy map and clears existing buildings
     * @param grid_width Grid width in cells
     * @param grid_height Grid height in cells
     */
    void reset(int grid_width, int grid_height);

    /**
     * @brief Returns whether a building definition can be placed at a cell
     * @param terrain Terrain data source
     * @param definition Building rule set
     * @param anchor Minimum cell for the footprint
     * @return True when placement is valid
     */
    bool canPlaceBuilding(const TerrainGrid& terrain,
                          const BuildingDefinition& definition,
                          const GridCoord& anchor) const;

    /**
     * @brief Places a building when the location is valid
     * @param terrain Terrain data source
     * @param definition Building rule set
     * @param anchor Minimum cell for the footprint
     * @return New building id or nullopt when placement fails
     */
    std::optional<std::uint32_t> placeBuilding(const TerrainGrid& terrain,
                                               const BuildingDefinition& definition,
                                               const GridCoord& anchor);

    /**
     * @brief Removes a building by id
     * @param building_id Building id
     * @return True when removal succeeds
     */
    bool removeBuilding(std::uint32_t building_id);

    /**
     * @brief Returns whether a grid cell is occupied by any building
     * @param cell Cell coordinate
     * @return True when occupied
     */
    bool isCellOccupied(const GridCoord& cell) const;

    /**
     * @brief Returns whether a cell is blocked for unit traversal by a building
     * @param cell Cell coordinate
     * @return True when the occupying building blocks movement
     */
    bool blocksMovement(const GridCoord& cell) const;

    /**
     * @brief Returns the occupying building id for a cell
     * @param cell Cell coordinate
     * @return Building id or 0 when empty/invalid
     */
    std::uint32_t buildingIdAtCell(const GridCoord& cell) const;

    /**
     * @brief Looks up one building instance by id
     * @param building_id Building id
     * @return Pointer to instance or nullptr
     */
    const BuildingInstance* findBuilding(std::uint32_t building_id) const;

    /**
     * @brief Looks up the building occupying one cell
     * @param cell Cell coordinate
     * @return Pointer to instance or nullptr
     */
    const BuildingInstance* findBuildingAtCell(const GridCoord& cell) const;

    /**
     * @brief Returns active building count
     * @return Number of placed buildings
     */
    std::size_t buildingCount() const;

private:
    /**
     * @brief Converts a valid cell coordinate into flat storage index
     * @param cell Cell coordinate
     * @return Flat vector index
     */
    std::size_t cellIndex(const GridCoord& cell) const;

    /**
     * @brief Reports whether a cell lies within the configured occupancy grid
     * @param cell Cell coordinate
     * @return True when valid
     */
    bool isValidCell(const GridCoord& cell) const;

    // occupancy grid dimensions
    int grid_width_;
    int grid_height_;
    // id source for new buildings
    std::uint32_t next_building_id_;
    // flat grid storing occupying building id or zero for empty
    std::vector<std::uint32_t> occupancy_;
    // full instance data keyed by building id
    std::unordered_map<std::uint32_t, BuildingInstance> buildings_;
};

#endif
