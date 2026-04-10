#include "TerrainGrid.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace {
// maps a terrain preset to the default gameplay values for one cell
TerrainCell make_cell_for_type(TerrainType type) {
    switch (type) {
    case TerrainType::grass:
        return TerrainCell{type, 1.0f, true, true, 0.0f};
    case TerrainType::road:
        return TerrainCell{type, 0.7f, true, true, 0.0f};
    case TerrainType::forest:
        return TerrainCell{type, 1.9f, true, false, 0.0f};
    case TerrainType::water:
        return TerrainCell{type, 1000000.0f, false, false, -0.2f};
    case TerrainType::rock:
    default:
        return TerrainCell{TerrainType::rock, 1000000.0f, false, false, 0.2f};
    }
}
}  // namespace

TerrainGrid::TerrainGrid(int width, int height, float cell_size, const glm::vec2& origin_xz)
    // negative sizes collapse to zero and tiny cell sizes clamp upward so division stays safe
    : width_(std::max(width, 0)),
      height_(std::max(height, 0)),
      cell_size_(std::max(cell_size, 0.0001f)),
      origin_xz_(origin_xz),
      // every new map starts as grass until callers paint more specific terrain on top
      cells_(static_cast<std::size_t>(std::max(width, 0) * std::max(height, 0)),
             make_cell_for_type(TerrainType::grass)) {}

int TerrainGrid::width() const {
    return width_;
}

int TerrainGrid::height() const {
    return height_;
}

float TerrainGrid::cellSize() const {
    return cell_size_;
}

glm::vec2 TerrainGrid::origin() const {
    return origin_xz_;
}

bool TerrainGrid::isValidCell(const GridCoord& cell) const {
    return cell.x >= 0 && cell.y >= 0 && cell.x < width_ && cell.y < height_;
}

bool TerrainGrid::setTerrainType(const GridCoord& cell, TerrainType type) {
    if (!isValidCell(cell)) {
        return false;
    }
    // changing the preset resets all derived default flags and costs for that cell
    cells_[cellIndex(cell)] = make_cell_for_type(type);
    return true;
}

bool TerrainGrid::setCellFlags(const GridCoord& cell, bool walkable, bool buildable) {
    if (!isValidCell(cell)) {
        return false;
    }
    TerrainCell& terrain_cell = cells_[cellIndex(cell)];
    // these flags are intentionally independent so a cell could be walkable but not buildable for example
    terrain_cell.walkable = walkable;
    terrain_cell.buildable = buildable;
    return true;
}

bool TerrainGrid::setMovementCost(const GridCoord& cell, float movement_cost) {
    if (!isValidCell(cell) || movement_cost <= 0.0f) {
        return false;
    }
    // movement cost must stay positive so pathfinding does not get zero or negative edge weights
    cells_[cellIndex(cell)].movement_cost = movement_cost;
    return true;
}

bool TerrainGrid::setElevation(const GridCoord& cell, float elevation) {
    if (!isValidCell(cell)) {
        return false;
    }
    // elevation is separate from terrain type so cliffs ramps or custom shaping can be layered in later
    cells_[cellIndex(cell)].elevation = elevation;
    return true;
}

TerrainCell TerrainGrid::cell(const GridCoord& cell) const {
    if (!isValidCell(cell)) {
        // invalid queries behave like solid rock so callers get a safe impassable default
        return make_cell_for_type(TerrainType::rock);
    }
    return cells_[cellIndex(cell)];
}

TerrainType TerrainGrid::terrainType(const GridCoord& cell) const {
    return this->cell(cell).type;
}

bool TerrainGrid::isWalkable(const GridCoord& cell) const {
    return this->cell(cell).walkable;
}

bool TerrainGrid::isBuildable(const GridCoord& cell) const {
    return this->cell(cell).buildable;
}

float TerrainGrid::movementCost(const GridCoord& cell) const {
    if (!isValidCell(cell)) {
        // invalid cells should lose every cost comparison by looking infinitely expensive
        return std::numeric_limits<float>::max();
    }
    return cells_[cellIndex(cell)].movement_cost;
}

float TerrainGrid::elevation(const GridCoord& cell) const {
    return this->cell(cell).elevation;
}

bool TerrainGrid::worldToCell(const glm::vec3& world_position, GridCoord& out_cell) const {
    // convert world x and z into local grid coordinates then floor into integer cell ids
    // floor means every square cell owns the half open interval from its minimum corner up to the next one
    const float local_x = (world_position.x - origin_xz_.x) / cell_size_;
    const float local_y = (world_position.z - origin_xz_.y) / cell_size_;
    const GridCoord cell{
        static_cast<int>(std::floor(local_x)),
        static_cast<int>(std::floor(local_y))
    };
    if (!isValidCell(cell)) {
        // points outside the grid do not modify out_cell
        return false;
    }
    out_cell = cell;
    return true;
}

glm::vec3 TerrainGrid::cellCenter(const GridCoord& cell) const {
    if (!isValidCell(cell)) {
        return glm::vec3(0.0f);
    }
    // y comes from stored elevation while x and z come from the geometric center of the square
    return glm::vec3(origin_xz_.x + (static_cast<float>(cell.x) + 0.5f) * cell_size_,
                     elevation(cell),
                     origin_xz_.y + (static_cast<float>(cell.y) + 0.5f) * cell_size_);
}

std::vector<GridCoord> TerrainGrid::cellsInFootprint(const GridCoord& anchor,
                                                     int footprint_width,
                                                     int footprint_height) const {
    std::vector<GridCoord> cells{};
    if (footprint_width <= 0 || footprint_height <= 0) {
        return cells;
    }

    const GridCoord max_cell{
        anchor.x + footprint_width - 1,
        anchor.y + footprint_height - 1
    };
    if (!isValidCell(anchor) || !isValidCell(max_cell)) {
        // the whole footprint must fit inside the map
        return cells;
    }

    // emit cells in row major order so callers can walk the footprint predictably
    cells.reserve(static_cast<std::size_t>(footprint_width * footprint_height));
    for (int y = 0; y < footprint_height; ++y) {
        for (int x = 0; x < footprint_width; ++x) {
            cells.push_back(GridCoord{anchor.x + x, anchor.y + y});
        }
    }
    return cells;
}

float TerrainGrid::averageMovementCost(const GridCoord& anchor,
                                       int footprint_width,
                                       int footprint_height) const {
    const std::vector<GridCoord> footprint =
        cellsInFootprint(anchor, footprint_width, footprint_height);
    if (footprint.empty()) {
        return std::numeric_limits<float>::max();
    }

    float total_cost = 0.0f;
    for (const GridCoord& cell : footprint) {
        // simple average is enough for coarse placement heuristics
        total_cost += movementCost(cell);
    }
    return total_cost / static_cast<float>(footprint.size());
}

std::size_t TerrainGrid::cellIndex(const GridCoord& cell) const {
    // row major layout means x changes fastest and y jumps by one full row
    // this keeps storage simple and makes a rectangular grid fit in one flat vector
    return static_cast<std::size_t>(cell.y * width_ + cell.x);
}
