#include "RtsPathfinder.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <queue>

namespace {
constexpr float kStraightStepCost = 1.0f;
constexpr float kDiagonalStepCost = 1.41421356f;
constexpr float kSlopePenaltyScale = 0.35f;

// one frontier entry in the A star open set
struct OpenNode {
    int cell_index;
    float priority;
};

// priority queue comparator so lowest priority value comes out first
struct OpenNodeCompare {
    bool operator()(const OpenNode& lhs, const OpenNode& rhs) const {
        return lhs.priority > rhs.priority;
    }
};

bool is_traversable(const TerrainGrid& terrain,
                    const BuildingSystem& buildings,
                    const GridCoord& cell) {
    return terrain.isValidCell(cell) &&
           terrain.isWalkable(cell) &&
           !buildings.blocksMovement(cell);
}

int flatten_index(const TerrainGrid& terrain, const GridCoord& cell) {
    // convert 2d cell coordinates into a compact flat array index
    return cell.y * terrain.width() + cell.x;
}

GridCoord expand_index(const TerrainGrid& terrain, int index) {
    // inverse of flatten_index
    return GridCoord{index % terrain.width(), index / terrain.width()};
}

float estimate_cost(const GridCoord& current, const GridCoord& goal) {
    // octile distance heuristic matches 8 way movement better than plain manhattan distance
    const float dx = static_cast<float>(std::abs(goal.x - current.x));
    const float dy = static_cast<float>(std::abs(goal.y - current.y));
    const float diagonal_steps = std::min(dx, dy);
    const float straight_steps = std::max(dx, dy) - diagonal_steps;
    return diagonal_steps * kDiagonalStepCost + straight_steps * kStraightStepCost;
}

float travel_cost(const TerrainGrid& terrain,
                  const GridCoord& from,
                  const GridCoord& to,
                  bool diagonal) {
    // movement cost blends step shape terrain resistance and elevation change
    const float step_cost = diagonal ? kDiagonalStepCost : kStraightStepCost;
    const float terrain_cost =
        0.5f * (terrain.movementCost(from) + terrain.movementCost(to));
    const float slope_penalty =
        std::fabs(terrain.elevation(to) - terrain.elevation(from)) * kSlopePenaltyScale;
    return step_cost * terrain_cost + slope_penalty;
}
}  // namespace

std::vector<GridCoord> RtsPathfinder::findPath(const TerrainGrid& terrain,
                                               const BuildingSystem& buildings,
                                               const GridCoord& start_cell,
                                               const GridCoord& goal_cell) const {
    std::vector<GridCoord> path{};
    if (terrain.width() <= 0 || terrain.height() <= 0 ||
        !is_traversable(terrain, buildings, start_cell) ||
        !is_traversable(terrain, buildings, goal_cell)) {
        return path;
    }

    if (start_cell.x == goal_cell.x && start_cell.y == goal_cell.y) {
        path.push_back(start_cell);
        return path;
    }

    const int grid_size = terrain.width() * terrain.height();
    const int start_index = flatten_index(terrain, start_cell);
    const int goal_index = flatten_index(terrain, goal_cell);

    // these arrays are the classic A star bookkeeping tables
    std::vector<float> g_score(static_cast<std::size_t>(grid_size),
                               std::numeric_limits<float>::max());
    std::vector<int> came_from(static_cast<std::size_t>(grid_size), -1);
    std::vector<bool> closed(static_cast<std::size_t>(grid_size), false);
    std::priority_queue<OpenNode, std::vector<OpenNode>, OpenNodeCompare> open_set{};

    g_score[static_cast<std::size_t>(start_index)] = 0.0f;
    open_set.push(OpenNode{start_index, estimate_cost(start_cell, goal_cell)});

    // 8 way movement lets units cut diagonally where corner clipping is not blocked
    static const std::array<GridCoord, 8> kNeighbors = {
        GridCoord{1, 0},
        GridCoord{-1, 0},
        GridCoord{0, 1},
        GridCoord{0, -1},
        GridCoord{1, 1},
        GridCoord{1, -1},
        GridCoord{-1, 1},
        GridCoord{-1, -1}
    };

    while (!open_set.empty()) {
        // always expand the cheapest frontier node next
        const OpenNode open = open_set.top();
        open_set.pop();
        if (closed[static_cast<std::size_t>(open.cell_index)]) {
            continue;
        }
        closed[static_cast<std::size_t>(open.cell_index)] = true;

        if (open.cell_index == goal_index) {
            break;
        }

        const GridCoord current = expand_index(terrain, open.cell_index);
        for (const GridCoord& delta : kNeighbors) {
            const GridCoord next{current.x + delta.x, current.y + delta.y};
            const bool diagonal = delta.x != 0 && delta.y != 0;
            if (!is_traversable(terrain, buildings, next)) {
                continue;
            }
            if (diagonal) {
                // forbid cutting through blocked corners by requiring both side cells to be open
                const GridCoord side_a{current.x + delta.x, current.y};
                const GridCoord side_b{current.x, current.y + delta.y};
                if (!is_traversable(terrain, buildings, side_a) ||
                    !is_traversable(terrain, buildings, side_b)) {
                    continue;
                }
            }

            const int next_index = flatten_index(terrain, next);
            if (closed[static_cast<std::size_t>(next_index)]) {
                continue;
            }

            // tentative_score is what the path cost would be if we reached next through current
            const float tentative_score =
                g_score[static_cast<std::size_t>(open.cell_index)] +
                travel_cost(terrain, current, next, diagonal);
            if (tentative_score >= g_score[static_cast<std::size_t>(next_index)]) {
                continue;
            }

            came_from[static_cast<std::size_t>(next_index)] = open.cell_index;
            g_score[static_cast<std::size_t>(next_index)] = tentative_score;
            open_set.push(OpenNode{
                next_index,
                tentative_score + estimate_cost(next, goal_cell)
            });
        }
    }

    if (came_from[static_cast<std::size_t>(goal_index)] == -1) {
        // goal was never reached
        return path;
    }

    // reconstruct by walking backwards from goal to start then reverse the result
    int current_index = goal_index;
    while (current_index != -1) {
        path.push_back(expand_index(terrain, current_index));
        if (current_index == start_index) {
            break;
        }
        current_index = came_from[static_cast<std::size_t>(current_index)];
    }
    std::reverse(path.begin(), path.end());
    return path;
}

std::vector<glm::vec3> RtsPathfinder::findWorldPath(const TerrainGrid& terrain,
                                                    const BuildingSystem& buildings,
                                                    const GridCoord& start_cell,
                                                    const GridCoord& goal_cell) const {
    const std::vector<GridCoord> cell_path = findPath(terrain, buildings, start_cell, goal_cell);
    std::vector<glm::vec3> world_path{};
    world_path.reserve(cell_path.size());
    for (const GridCoord& cell : cell_path) {
        world_path.push_back(terrain.cellCenter(cell));
    }
    return world_path;
}
