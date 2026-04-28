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

glm::vec2 world_to_local_grid(const TerrainGrid& terrain, const glm::vec3& position) {
    const glm::vec2 origin = terrain.origin();
    const float inverse_cell_size = 1.0f / terrain.cellSize();
    return glm::vec2((position.x - origin.x) * inverse_cell_size,
                     (position.z - origin.y) * inverse_cell_size);
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
    if (world_path.size() <= 2) {
        return world_path;
    }

    // collapse intermediate waypoints whenever the straight segment stays on traversable cells
    std::vector<glm::vec3> smoothed_path{};
    smoothed_path.reserve(world_path.size());
    std::size_t anchor_index = 0;
    smoothed_path.push_back(world_path.front());
    while (anchor_index + 1 < world_path.size()) {
        std::size_t furthest_visible_index = anchor_index + 1;
        for (std::size_t candidate_index = world_path.size() - 1;
             candidate_index > anchor_index + 1;
             --candidate_index) {
            if (hasLineOfSight(terrain,
                               buildings,
                               world_path[anchor_index],
                               world_path[candidate_index])) {
                furthest_visible_index = candidate_index;
                break;
            }
        }
        smoothed_path.push_back(world_path[furthest_visible_index]);
        anchor_index = furthest_visible_index;
    }
    return smoothed_path;
}

bool RtsPathfinder::hasLineOfSight(const TerrainGrid& terrain,
                                   const BuildingSystem& buildings,
                                   const glm::vec3& start,
                                   const glm::vec3& goal) const {
    GridCoord start_cell{};
    GridCoord goal_cell{};
    if (!terrain.worldToCell(start, start_cell) ||
        !terrain.worldToCell(goal, goal_cell) ||
        !is_traversable(terrain, buildings, start_cell) ||
        !is_traversable(terrain, buildings, goal_cell)) {
        return false;
    }
    if (start_cell.x == goal_cell.x && start_cell.y == goal_cell.y) {
        return true;
    }

    const glm::vec2 start_grid = world_to_local_grid(terrain, start);
    const glm::vec2 goal_grid = world_to_local_grid(terrain, goal);
    const glm::vec2 delta = goal_grid - start_grid;
    const float infinity = std::numeric_limits<float>::infinity();
    const float epsilon = 0.00001f;

    GridCoord current = start_cell;
    const int step_x = delta.x > 0.0f ? 1 : (delta.x < 0.0f ? -1 : 0);
    const int step_y = delta.y > 0.0f ? 1 : (delta.y < 0.0f ? -1 : 0);
    const float t_delta_x = step_x == 0 ? infinity : 1.0f / std::fabs(delta.x);
    const float t_delta_y = step_y == 0 ? infinity : 1.0f / std::fabs(delta.y);
    const float next_boundary_x =
        step_x > 0 ? std::floor(start_grid.x) + 1.0f : std::floor(start_grid.x);
    const float next_boundary_y =
        step_y > 0 ? std::floor(start_grid.y) + 1.0f : std::floor(start_grid.y);
    float t_max_x =
        step_x == 0 ? infinity : (next_boundary_x - start_grid.x) / delta.x;
    float t_max_y =
        step_y == 0 ? infinity : (next_boundary_y - start_grid.y) / delta.y;

    while (!(current.x == goal_cell.x && current.y == goal_cell.y)) {
        if (std::fabs(t_max_x - t_max_y) <= epsilon) {
            const GridCoord side_x{current.x + step_x, current.y};
            const GridCoord side_y{current.x, current.y + step_y};
            const GridCoord diagonal{current.x + step_x, current.y + step_y};
            if (!is_traversable(terrain, buildings, side_x) ||
                !is_traversable(terrain, buildings, side_y) ||
                !is_traversable(terrain, buildings, diagonal)) {
                return false;
            }
            current = diagonal;
            t_max_x += t_delta_x;
            t_max_y += t_delta_y;
            continue;
        }

        if (t_max_x < t_max_y) {
            current.x += step_x;
            t_max_x += t_delta_x;
        } else {
            current.y += step_y;
            t_max_y += t_delta_y;
        }

        if (!is_traversable(terrain, buildings, current)) {
            return false;
        }
    }
    return true;
}
