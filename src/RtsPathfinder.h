/**
 * @file RtsPathfinder.h
 * @brief a star pathfinding over terrain costs and building blockers
 */
#ifndef RTS_PATHFINDER_H
#define RTS_PATHFINDER_H

#include <vector>

#include <glm/glm.hpp>

#include "BuildingSystem.h"
#include "TerrainGrid.h"

/**
 * @brief grid based path planner for rts unit movement
 *
 * this planner works in cell space first
 * then optionally converts the result into world points at cell centers
 */
class RtsPathfinder {
public:
    /**
     * @brief finds a cell path between two traversable cells
     * @param terrain Terrain data source
     * @param buildings Building occupancy source
     * @param start_cell Start cell
     * @param goal_cell Goal cell
     * @return Cell path including start and goal, or empty when unreachable
     */
    std::vector<GridCoord> findPath(const TerrainGrid& terrain,
                                    const BuildingSystem& buildings,
                                    const GridCoord& start_cell,
                                    const GridCoord& goal_cell) const;

    /**
     * @brief finds a world space path by converting the cell path into cell centers
     * @param terrain Terrain data source
     * @param buildings Building occupancy source
     * @param start_cell Start cell
     * @param goal_cell Goal cell
     * @return World-space cell-center path including start and goal, or empty when unreachable
     */
    std::vector<glm::vec3> findWorldPath(const TerrainGrid& terrain,
                                         const BuildingSystem& buildings,
                                         const GridCoord& start_cell,
                                         const GridCoord& goal_cell) const;

    /**
     * @brief Returns whether a direct world-space segment can stay on traversable cells
     * @param terrain Terrain data source
     * @param buildings Building occupancy source
     * @param start World-space start point
     * @param goal World-space end point
     * @return True when the segment does not cross blocked cells or clipped corners
     */
    bool hasLineOfSight(const TerrainGrid& terrain,
                        const BuildingSystem& buildings,
                        const glm::vec3& start,
                        const glm::vec3& goal) const;
};

#endif
