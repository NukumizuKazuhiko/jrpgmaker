#pragma once

#include <cstddef>
#include <vector>

#include <glm/glm.hpp>

namespace jrpgmaker::core {

struct GridCell {
    int x = 0;
    int y = 0;
    friend bool operator==(const GridCell&, const GridCell&) = default;
};

enum class PathFailure {
    kNone,
    kStartOutOfBounds,
    kGoalOutOfBounds,
    kStartBlocked,
    kGoalBlocked,
    kSearchLimit,
    kUnreachable,
};

struct PathResult {
    std::vector<GridCell> cells;
    PathFailure failure = PathFailure::kNone;

    [[nodiscard]] bool succeeded() const { return failure == PathFailure::kNone; }
};

class NavigationGrid {
public:
    NavigationGrid(int width, int height, glm::vec2 origin, float cell_size,
                   std::vector<bool> walkable);

    int width() const { return width_; }
    int height() const { return height_; }
    bool InBounds(GridCell cell) const;
    bool IsWalkable(GridCell cell) const;
    glm::vec3 CellToWorld(GridCell cell, float y = 0.0f) const;
    GridCell WorldToCell(glm::vec3 position) const;

    [[nodiscard]] PathResult FindPath(GridCell start, GridCell goal,
                                      std::size_t max_nodes = 65536) const;

private:
    int width_;
    int height_;
    glm::vec2 origin_;
    float cell_size_;
    std::vector<bool> walkable_;
};

} // namespace jrpgmaker::core
