/**
 * @file FogOfWar.h
 * @brief per team fog of war grid with unexplored explored and visible states
 */
#ifndef FOG_OF_WAR_H
#define FOG_OF_WAR_H

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

#include "TerrainGrid.h"

/**
 * @brief fog state for one cell for one team
 */
enum class VisibilityState : std::uint8_t {
    unexplored = 0,  // Never seen (black)
    explored = 1,    // Previously seen (grey)
    visible = 2      // Currently visible (full)
};

/**
 * @brief tracks visibility separately for each team
 *
 * the normal frame pattern is
 * clear current visible cells back to explored
 * then reveal fresh circles around friendly units and buildings
 */
class FogOfWar {
public:
    /**
     * @brief Constructs a fog of war grid
     * @param grid_width Grid width in cells
     * @param grid_height Grid height in cells
     * @param team_count Number of teams to track
     */
    FogOfWar(int grid_width, int grid_height, int team_count = 2);

    /**
     * @brief Returns visibility state for a cell for a team
     * @param team Team id
     * @param cell Grid coordinate
     * @return Visibility state
     */
    VisibilityState cellVisibility(int team, const GridCoord& cell) const;

    /**
     * @brief Returns whether a cell is currently visible to a team
     * @param team Team id
     * @param cell Grid coordinate
     * @return True when visible
     */
    bool isVisible(int team, const GridCoord& cell) const;

    /**
     * @brief Returns whether a cell has been explored by a team
     * @param team Team id
     * @param cell Grid coordinate
     * @return True when explored or visible
     */
    bool isExplored(int team, const GridCoord& cell) const;

    /**
     * @brief Returns whether a world position is visible to a team
     * @param team Team id
     * @param pos World position
     * @param terrain Terrain grid for coordinate conversion
     * @return True when visible
     */
    bool isPositionVisible(int team, const glm::vec3& pos, const TerrainGrid& terrain) const;

    /**
     * @brief Demotes all visible cells to explored for a team
     * @param team Team id
     */
    void clearCurrentVision(int team);

    /**
     * @brief Reveals a circular area centered on a cell
     * @param team Team id
     * @param center Center cell coordinate
     * @param radius_cells Vision radius in cells
     */
    void revealCircle(int team, const GridCoord& center, int radius_cells);

    /**
     * @brief Returns grid width
     * @return Width in cells
     */
    int width() const;

    /**
     * @brief Returns grid height
     * @return Height in cells
     */
    int height() const;

private:
    /**
     * @brief Converts team and cell to flat storage index
     * @param team Team id
     * @param cell Grid coordinate
     * @return Flat index or -1 when invalid
     */
    int cellIndex(int team, const GridCoord& cell) const;

    /**
     * @brief Precomputes circle offsets for all radii up to max
     */
    void precomputeCircleOffsets();

    // fog grid dimensions
    int grid_width_;
    int grid_height_;
    // number of tracked teams
    int team_count_;
    // flat storage of all team cell states
    std::vector<VisibilityState> team_visibility_;  // flat: team * grid_size + cell_index
    // precomputed filled circle offsets for each reveal radius
    std::vector<std::vector<GridCoord>> circle_offsets_;  // precomputed for each radius

    // largest reveal radius with precomputed offsets
    static constexpr int kMaxRadius = 15;
};

#endif
