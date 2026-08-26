#include "jrpgmaker/core/pathfinding.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>
#include <stdexcept>
#include <utility>

namespace jrpgmaker::core {

namespace {

std::size_t Index(GridCell cell, int width) {
    return static_cast<std::size_t>(cell.y) * static_cast<std::size_t>(width) +
           static_cast<std::size_t>(cell.x);
}

struct QueueEntry {
    GridCell cell;
    int cost;
    int estimate;
};

struct QueueCompare {
    bool operator()(const QueueEntry& left, const QueueEntry& right) const {
        if (left.estimate != right.estimate) {
            return left.estimate > right.estimate;
        }
        if (left.cost != right.cost) {
            return left.cost > right.cost;
        }
        if (left.cell.y != right.cell.y) {
            return left.cell.y > right.cell.y;
        }
        return left.cell.x > right.cell.x;
    }
};

int Heuristic(GridCell a, GridCell b) {
    return std::abs(a.x - b.x) + std::abs(a.y - b.y);
}

} // namespace

NavigationGrid::NavigationGrid(int width, int height, glm::vec2 origin, float cell_size,
                               std::vector<bool> walkable)
    : width_(width), height_(height), origin_(origin), cell_size_(cell_size),
      walkable_(std::move(walkable)) {
    if (width_ <= 0 || height_ <= 0 || cell_size_ <= 0.0f ||
        static_cast<std::size_t>(width_) >
            std::numeric_limits<std::size_t>::max() / static_cast<std::size_t>(height_) ||
        walkable_.size() != static_cast<std::size_t>(width_) * height_) {
        throw std::invalid_argument("core: invalid navigation grid dimensions");
    }
}

bool NavigationGrid::InBounds(GridCell cell) const {
    return cell.x >= 0 && cell.x < width_ && cell.y >= 0 && cell.y < height_;
}

bool NavigationGrid::IsWalkable(GridCell cell) const {
    return InBounds(cell) && walkable_[Index(cell, width_)];
}

glm::vec3 NavigationGrid::CellToWorld(GridCell cell, float y) const {
    return {origin_.x + (static_cast<float>(cell.x) + 0.5f) * cell_size_, y,
            origin_.y + (static_cast<float>(cell.y) + 0.5f) * cell_size_};
}

GridCell NavigationGrid::WorldToCell(glm::vec3 position) const {
    return {static_cast<int>(std::floor((position.x - origin_.x) / cell_size_)),
            static_cast<int>(std::floor((position.z - origin_.y) / cell_size_))};
}

PathResult NavigationGrid::FindPath(GridCell start, GridCell goal, std::size_t max_nodes) const {
    PathResult result;
    if (!InBounds(start)) {
        result.failure = PathFailure::kStartOutOfBounds;
        return result;
    }
    if (!InBounds(goal)) {
        result.failure = PathFailure::kGoalOutOfBounds;
        return result;
    }
    if (!IsWalkable(start)) {
        result.failure = PathFailure::kStartBlocked;
        return result;
    }
    if (!IsWalkable(goal)) {
        result.failure = PathFailure::kGoalBlocked;
        return result;
    }
    if (max_nodes == 0u) {
        result.failure = PathFailure::kSearchLimit;
        return result;
    }
    if (start == goal) {
        result.cells.push_back(start);
        return result;
    }

    const std::size_t cell_count =
        static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_);
    std::vector<int> best_cost(cell_count, std::numeric_limits<int>::max());
    std::vector<GridCell> parent(cell_count, GridCell{-1, -1});
    std::priority_queue<QueueEntry, std::vector<QueueEntry>, QueueCompare> open;
    best_cost[Index(start, width_)] = 0;
    open.push({start, 0, Heuristic(start, goal)});
    std::size_t visited = 0;
    constexpr GridCell directions[] = {{0, -1}, {-1, 0}, {1, 0}, {0, 1}};

    while (!open.empty() && visited < max_nodes) {
        const QueueEntry current = open.top();
        open.pop();
        if (current.cost != best_cost[Index(current.cell, width_)]) {
            continue;
        }
        ++visited;
        if (current.cell == goal) {
            for (GridCell cell = goal;; cell = parent[Index(cell, width_)]) {
                result.cells.push_back(cell);
                if (cell == start) {
                    break;
                }
            }
            std::reverse(result.cells.begin(), result.cells.end());
            return result;
        }
        for (const GridCell direction : directions) {
            const GridCell next{current.cell.x + direction.x, current.cell.y + direction.y};
            if (!IsWalkable(next)) {
                continue;
            }
            const int cost = current.cost + 1;
            int& best = best_cost[Index(next, width_)];
            if (cost >= best) {
                continue;
            }
            best = cost;
            parent[Index(next, width_)] = current.cell;
            open.push({next, cost, cost + Heuristic(next, goal)});
        }
    }
    result.failure = visited >= max_nodes ? PathFailure::kSearchLimit : PathFailure::kUnreachable;
    return result;
}

} // namespace jrpgmaker::core
