#include "detect/clustering.h"

#include <cmath>
#include <cstdint>
#include <unordered_map>

namespace sillage {

namespace {

int64_t cellKey(int32_t cx, int32_t cy) {
    return (static_cast<int64_t>(cx) << 32) ^ (static_cast<int64_t>(cy) & 0xffffffff);
}

} // namespace

std::vector<Cluster> clusterPoints(const std::vector<WorldPoint>& points,
                                   const ClusteringParams& params) {
    const float cell = params.linkDistance;
    const float linkSq = params.linkDistance * params.linkDistance;
    const size_t n = points.size();

    // Bucket point indices by grid cell.
    std::unordered_map<int64_t, std::vector<uint32_t>> grid;
    grid.reserve(n);
    auto cellOf = [cell](Vec2 p) {
        return std::pair{static_cast<int32_t>(std::floor(p.x / cell)),
                         static_cast<int32_t>(std::floor(p.y / cell))};
    };
    for (uint32_t i = 0; i < n; ++i) {
        const auto [cx, cy] = cellOf(points[i].pos);
        grid[cellKey(cx, cy)].push_back(i);
    }

    // Flood-fill connected components over the 3x3 cell neighborhood.
    std::vector<uint32_t> label(n, UINT32_MAX);
    std::vector<uint32_t> stack;
    std::vector<Cluster> clusters;

    for (uint32_t seed = 0; seed < n; ++seed) {
        if (label[seed] != UINT32_MAX) {
            continue;
        }
        const auto clusterIndex = static_cast<uint32_t>(clusters.size());
        label[seed] = clusterIndex;
        stack.assign(1, seed);

        Vec2 sum{};
        uint32_t count = 0;
        std::vector<Vec2> members;

        while (!stack.empty()) {
            const uint32_t i = stack.back();
            stack.pop_back();
            const Vec2 p = points[i].pos;
            sum = sum + p;
            ++count;
            members.push_back(p);

            const auto [cx, cy] = cellOf(p);
            for (int32_t dx = -1; dx <= 1; ++dx) {
                for (int32_t dy = -1; dy <= 1; ++dy) {
                    const auto it = grid.find(cellKey(cx + dx, cy + dy));
                    if (it == grid.end()) {
                        continue;
                    }
                    for (const uint32_t j : it->second) {
                        if (label[j] != UINT32_MAX) {
                            continue;
                        }
                        if ((points[j].pos - p).normSq() <= linkSq) {
                            label[j] = clusterIndex;
                            stack.push_back(j);
                        }
                    }
                }
            }
        }

        if (count < params.minPoints) {
            clusters.emplace_back(); // placeholder, removed below
            clusters.back().pointCount = 0;
            continue;
        }

        Cluster c;
        c.centroid = sum * (1.0f / static_cast<float>(count));
        c.pointCount = count;
        float spread = 0.0f;
        for (const Vec2 m : members) {
            spread += (m - c.centroid).normSq();
        }
        c.radius = std::sqrt(spread / static_cast<float>(count));
        clusters.push_back(c);
    }

    // Drop noise placeholders.
    std::erase_if(clusters, [](const Cluster& c) { return c.pointCount == 0; });
    return clusters;
}

} // namespace sillage
