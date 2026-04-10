#include "BuildingSystem.h"

#include <algorithm>

BuildingSystem::BuildingSystem(int grid_width, int grid_height)
    : grid_width_(std::max(grid_width, 0)),
      grid_height_(std::max(grid_height, 0)),
      next_building_id_(1),
      occupancy_(static_cast<std::size_t>(std::max(grid_width, 0) * std::max(grid_height, 0)), 0),
      buildings_() {}

void BuildingSystem::reset(int grid_width, int grid_height) {
    // reset throws away every placed building and rebuilds a clean occupancy grid
    grid_width_ = std::max(grid_width, 0);
    grid_height_ = std::max(grid_height, 0);
    next_building_id_ = 1;
    occupancy_.assign(static_cast<std::size_t>(grid_width_ * grid_height_), 0);
    buildings_.clear();
}

bool BuildingSystem::canPlaceBuilding(const TerrainGrid& terrain,
                                      const BuildingDefinition& definition,
                                      const GridCoord& anchor) const {
    if (definition.footprint_width <= 0 || definition.footprint_height <= 0) {
        return false;
    }
    if (terrain.width() != grid_width_ || terrain.height() != grid_height_) {
        return false;
    }

    const std::vector<GridCoord> footprint =
        terrain.cellsInFootprint(anchor, definition.footprint_width, definition.footprint_height);
    if (footprint.empty()) {
        return false;
    }

    // every covered cell must be empty and optionally buildable depending on the definition
    for (const GridCoord& cell : footprint) {
        if (isCellOccupied(cell)) {
            return false;
        }
        if (definition.requires_buildable_ground && !terrain.isBuildable(cell)) {
            return false;
        }
    }
    return true;
}

std::optional<std::uint32_t> BuildingSystem::placeBuilding(const TerrainGrid& terrain,
                                                           const BuildingDefinition& definition,
                                                           const GridCoord& anchor) {
    if (!canPlaceBuilding(terrain, definition, anchor)) {
        return std::nullopt;
    }

    // write the new building id into every occupied cell so later lookups are constant time
    const std::uint32_t building_id = next_building_id_++;
    const std::vector<GridCoord> footprint =
        terrain.cellsInFootprint(anchor, definition.footprint_width, definition.footprint_height);
    for (const GridCoord& cell : footprint) {
        occupancy_[cellIndex(cell)] = building_id;
    }

    BuildingInstance building{};
    building.id = building_id;
    building.anchor = anchor;
    building.footprint_width = definition.footprint_width;
    building.footprint_height = definition.footprint_height;
    building.blocks_movement = definition.blocks_movement;
    buildings_[building_id] = building;
    return building_id;
}

bool BuildingSystem::removeBuilding(std::uint32_t building_id) {
    const auto it = buildings_.find(building_id);
    if (it == buildings_.end()) {
        return false;
    }

    // clear only cells that still point at this id in case the grid was partially modified elsewhere
    const BuildingInstance& building = it->second;
    for (int y = 0; y < building.footprint_height; ++y) {
        for (int x = 0; x < building.footprint_width; ++x) {
            const GridCoord cell{building.anchor.x + x, building.anchor.y + y};
            if (isValidCell(cell) && occupancy_[cellIndex(cell)] == building_id) {
                occupancy_[cellIndex(cell)] = 0;
            }
        }
    }

    buildings_.erase(it);
    return true;
}

bool BuildingSystem::isCellOccupied(const GridCoord& cell) const {
    if (!isValidCell(cell)) {
        return false;
    }
    return occupancy_[cellIndex(cell)] != 0;
}

bool BuildingSystem::blocksMovement(const GridCoord& cell) const {
    if (!isValidCell(cell)) {
        return false;
    }

    // occupancy and movement blocking are separate because some buildings could be decorative
    const std::uint32_t building_id = occupancy_[cellIndex(cell)];
    if (building_id == 0) {
        return false;
    }
    const auto it = buildings_.find(building_id);
    if (it == buildings_.end()) {
        return false;
    }
    return it->second.blocks_movement;
}

std::uint32_t BuildingSystem::buildingIdAtCell(const GridCoord& cell) const {
    if (!isValidCell(cell)) {
        return 0;
    }
    return occupancy_[cellIndex(cell)];
}

const BuildingInstance* BuildingSystem::findBuilding(std::uint32_t building_id) const {
    const auto it = buildings_.find(building_id);
    if (it == buildings_.end()) {
        return nullptr;
    }
    return &it->second;
}

const BuildingInstance* BuildingSystem::findBuildingAtCell(const GridCoord& cell) const {
    const std::uint32_t building_id = buildingIdAtCell(cell);
    if (building_id == 0) {
        return nullptr;
    }
    return findBuilding(building_id);
}

std::size_t BuildingSystem::buildingCount() const {
    return buildings_.size();
}

std::size_t BuildingSystem::cellIndex(const GridCoord& cell) const {
    // flat indexing keeps occupancy storage compact and cache friendly
    return static_cast<std::size_t>(cell.y * grid_width_ + cell.x);
}

bool BuildingSystem::isValidCell(const GridCoord& cell) const {
    return cell.x >= 0 && cell.y >= 0 && cell.x < grid_width_ && cell.y < grid_height_;
}
