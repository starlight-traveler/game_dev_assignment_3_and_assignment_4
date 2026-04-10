/**
 * @file TerrainGrid.h
 * @brief grid terrain data for movement buildability and elevation
 */
#ifndef TERRAIN_GRID_H
#define TERRAIN_GRID_H

#include <vector>

#include <glm/glm.hpp>

/**
 * @brief integer cell coordinate in terrain space
 *
 * x moves across the grid horizontally
 * y moves down the grid in the same direction as world z
 */
struct GridCoord {
    // column index in terrain space
    int x;
    // row index in terrain space which maps to world z not world y
    int y;
};

/**
 * @brief terrain preset used to seed default cell values
 *
 * these presets are just convenient starting points
 * callers can still override movement cost flags or elevation afterward
 */
enum class TerrainType {
    // default open ground
    grass,
    // travel lane with cheaper movement
    road,
    // slower natural terrain that usually blocks building
    forest,
    // blocked low terrain
    water,
    // blocked hard terrain and invalid query fallback
    rock
};

/**
 * @brief gameplay data stored for one terrain cell
 *
 * type is the high level label
 * movement_cost affects how fast units cross the cell
 * walkable says whether units may path through it at all
 * buildable says whether buildings may be placed there
 * elevation becomes the y value for cellCenter and snapped unit positions
 */
struct TerrainCell {
    // coarse semantic label for the cell
    TerrainType type;
    // larger values mean slower movement through this cell
    float movement_cost;
    // whether pathfinding may step onto this cell
    bool walkable;
    // whether building placement may include this cell
    bool buildable;
    // world space y value used when sampling this cell
    float elevation;
};

/**
 * @brief owns the terrain cells and world to grid conversion helpers
 *
 * pathfinding reads walkable movement_cost and elevation
 * building placement reads buildable
 * other systems use cellCenter and worldToCell to move between spaces
 */
class TerrainGrid {
public:
    /**
     * @brief Constructs a terrain grid with default grass cells
     * @param width Grid width in cells
     * @param height Grid height in cells
     * @param cell_size World-space size of each cell
     * @param origin_xz Minimum world-space XZ corner of cell (0, 0)
     */
    TerrainGrid(int width, int height, float cell_size, const glm::vec2& origin_xz = glm::vec2(0.0f));

    /**
     * @brief Returns grid width in cells
     * @return Width
     */
    int width() const;

    /**
     * @brief Returns grid height in cells
     * @return Height
     */
    int height() const;

    /**
     * @brief Returns one cell's world-space side length
     * @return Cell size
     */
    float cellSize() const;

    /**
     * @brief Returns minimum world-space XZ corner of the grid
     * @return Grid origin
     */
    glm::vec2 origin() const;

    /**
     * @brief Reports whether a cell coordinate lies within the grid
     * @param cell Cell coordinate
     * @return True when valid
     */
    bool isValidCell(const GridCoord& cell) const;

    /**
     * @brief Applies one terrain preset to a cell
     * @param cell Cell coordinate
     * @param type Terrain preset
     * @return True when updated
     */
    bool setTerrainType(const GridCoord& cell, TerrainType type);

    /**
     * @brief Overrides walkable and buildable flags for one cell
     * @param cell Cell coordinate
     * @param walkable Walkable flag
     * @param buildable Buildable flag
     * @return True when updated
     */
    bool setCellFlags(const GridCoord& cell, bool walkable, bool buildable);

    /**
     * @brief Overrides movement cost for one cell
     * @param cell Cell coordinate
     * @param movement_cost Positive movement cost multiplier
     * @return True when updated
     */
    bool setMovementCost(const GridCoord& cell, float movement_cost);

    /**
     * @brief Overrides elevation for one cell
     * @param cell Cell coordinate
     * @param elevation Elevation value
     * @return True when updated
     */
    bool setElevation(const GridCoord& cell, float elevation);

    /**
     * @brief Returns full terrain cell data
     * @param cell Cell coordinate
     * @return Cell data or default impassable rock-style data when invalid
     */
    TerrainCell cell(const GridCoord& cell) const;

    /**
     * @brief Returns terrain type for a cell
     * @param cell Cell coordinate
     * @return Terrain type or rock when invalid
     */
    TerrainType terrainType(const GridCoord& cell) const;

    /**
     * @brief Returns whether a cell can be traversed by units
     * @param cell Cell coordinate
     * @return Walkable flag
     */
    bool isWalkable(const GridCoord& cell) const;

    /**
     * @brief Returns whether a cell can host building placement
     * @param cell Cell coordinate
     * @return Buildable flag
     */
    bool isBuildable(const GridCoord& cell) const;

    /**
     * @brief Returns unit movement cost for a cell
     * @param cell Cell coordinate
     * @return Positive cost, or a very large value when invalid
     */
    float movementCost(const GridCoord& cell) const;

    /**
     * @brief Returns cell elevation
     * @param cell Cell coordinate
     * @return Elevation value
     */
    float elevation(const GridCoord& cell) const;

    /**
     * @brief Converts a world-space point into grid coordinates
     * @param world_position World-space position, using X and Z
     * @param out_cell Output cell coordinate
     * @return True when the position lies inside the grid
     */
    bool worldToCell(const glm::vec3& world_position, GridCoord& out_cell) const;

    /**
     * @brief Returns world-space center point of a cell
     * @param cell Cell coordinate
     * @return Cell center in XZ plus stored elevation on Y
     */
    glm::vec3 cellCenter(const GridCoord& cell) const;

    /**
     * @brief Returns all cells covered by an axis-aligned footprint
     * @param anchor Minimum cell of the footprint
     * @param footprint_width Width in cells
     * @param footprint_height Height in cells
     * @return Covered cells, or empty when the footprint is invalid
     */
    std::vector<GridCoord> cellsInFootprint(const GridCoord& anchor,
                                            int footprint_width,
                                            int footprint_height) const;

    /**
     * @brief Computes average movement cost over a footprint
     * @param anchor Minimum cell of the footprint
     * @param footprint_width Width in cells
     * @param footprint_height Height in cells
     * @return Average cost, or a very large value when the footprint is invalid
     */
    float averageMovementCost(const GridCoord& anchor,
                              int footprint_width,
                              int footprint_height) const;

private:
    /**
     * @brief Converts a valid cell coordinate into flat storage index
     * @param cell Cell coordinate
     * @return Flat vector index
     */
    std::size_t cellIndex(const GridCoord& cell) const;

    // number of columns in the map
    int width_;
    // number of rows in the map
    int height_;
    // world space side length of one square cell
    float cell_size_;
    // minimum world xz corner of the grid
    glm::vec2 origin_xz_;
    // flat row major terrain cell storage where x changes fastest
    std::vector<TerrainCell> cells_;
};

#endif
