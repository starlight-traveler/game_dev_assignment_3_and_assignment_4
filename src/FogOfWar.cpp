#include "FogOfWar.h"

#include <algorithm>
#include <cstddef>

FogOfWar::FogOfWar(int grid_width, int grid_height, int team_count)
    : grid_width_(grid_width),
      grid_height_(grid_height),
      team_count_(team_count),
      team_visibility_(static_cast<std::size_t>(team_count * grid_width * grid_height),
                       VisibilityState::unexplored),
      circle_offsets_(kMaxRadius + 1) {
    precomputeCircleOffsets();
}

void FogOfWar::precomputeCircleOffsets() {
    // revealCircle is called often
    // precomputing offsets avoids rebuilding the same disk shape every frame
    for (int r = 0; r <= kMaxRadius; ++r) {
        for (int dy = -r; dy <= r; ++dy) {
            for (int dx = -r; dx <= r; ++dx) {
                if (dx * dx + dy * dy <= r * r) {
                    circle_offsets_[static_cast<std::size_t>(r)].push_back({dx, dy});
                }
            }
        }
    }
}

int FogOfWar::cellIndex(int team, const GridCoord& cell) const {
    if (team < 0 || team >= team_count_) {
        return -1;
    }
    if (cell.x < 0 || cell.x >= grid_width_ || cell.y < 0 || cell.y >= grid_height_) {
        return -1;
    }
    // storage is laid out as all cells for team 0 then all cells for team 1 and so on
    return team * (grid_width_ * grid_height_) + cell.y * grid_width_ + cell.x;
}

VisibilityState FogOfWar::cellVisibility(int team, const GridCoord& cell) const {
    const int idx = cellIndex(team, cell);
    if (idx < 0) {
        return VisibilityState::unexplored;
    }
    return team_visibility_[static_cast<std::size_t>(idx)];
}

bool FogOfWar::isVisible(int team, const GridCoord& cell) const {
    return cellVisibility(team, cell) == VisibilityState::visible;
}

bool FogOfWar::isExplored(int team, const GridCoord& cell) const {
    const VisibilityState state = cellVisibility(team, cell);
    return state == VisibilityState::explored || state == VisibilityState::visible;
}

bool FogOfWar::isPositionVisible(int team, const glm::vec3& pos, const TerrainGrid& terrain) const {
    GridCoord cell{};
    if (!terrain.worldToCell(pos, cell)) {
        return false;
    }
    return isVisible(team, cell);
}

void FogOfWar::clearCurrentVision(int team) {
    if (team < 0 || team >= team_count_) {
        return;
    }
    // current visible cells fade back to explored before fresh vision is applied this frame
    const int base_offset = team * (grid_width_ * grid_height_);
    const int grid_size = grid_width_ * grid_height_;
    for (int i = 0; i < grid_size; ++i) {
        const std::size_t idx = static_cast<std::size_t>(base_offset + i);
        if (team_visibility_[idx] == VisibilityState::visible) {
            team_visibility_[idx] = VisibilityState::explored;
        }
    }
}

void FogOfWar::revealCircle(int team, const GridCoord& center, int radius_cells) {
    if (team < 0 || team >= team_count_) {
        return;
    }
    if (radius_cells < 0) {
        return;
    }
    const int clamped_radius = radius_cells > kMaxRadius ? kMaxRadius : radius_cells;
    const std::vector<GridCoord>& offsets =
        circle_offsets_[static_cast<std::size_t>(clamped_radius)];

    // each offset marks one cell currently visible for this team
    for (const GridCoord& offset : offsets) {
        const GridCoord cell{center.x + offset.x, center.y + offset.y};
        const int idx = cellIndex(team, cell);
        if (idx >= 0) {
            team_visibility_[static_cast<std::size_t>(idx)] = VisibilityState::visible;
        }
    }
}

int FogOfWar::width() const {
    return grid_width_;
}

int FogOfWar::height() const {
    return grid_height_;
}

int FogOfWar::teamCount() const {
    return team_count_;
}

void FogOfWar::ensureTeamCount(int team_count) {
    if (team_count <= team_count_) {
        return;
    }

    const int grid_size = grid_width_ * grid_height_;
    std::vector<VisibilityState> expanded(
        static_cast<std::size_t>(team_count * grid_size),
        VisibilityState::unexplored);
    for (int team = 0; team < team_count_; ++team) {
        const auto source_begin =
            team_visibility_.begin() + static_cast<std::ptrdiff_t>(team * grid_size);
        const auto source_end = source_begin + static_cast<std::ptrdiff_t>(grid_size);
        auto dest_begin =
            expanded.begin() + static_cast<std::ptrdiff_t>(team * grid_size);
        std::copy(source_begin, source_end, dest_begin);
    }

    team_visibility_.swap(expanded);
    team_count_ = team_count;
}
