#include "detect/clustering.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <unordered_map>

namespace sillage {

namespace {

int64_t cellKey(int32_t cx, int32_t cy) {
    return (static_cast<int64_t>(cx) << 32) ^ (static_cast<int64_t>(cy) & 0xffffffff);
}

Cluster makeCluster(const std::vector<WorldPoint>& points, std::vector<uint32_t> members) {
    Cluster c;
    if (members.empty()) {
        return c; // no NaN centroid/radius from a division by zero
    }
    c.pointCount = static_cast<uint32_t>(members.size());
    Vec2 sum{};
    for (const uint32_t i : members) {
        sum = sum + points[i].pos;
    }
    c.centroid = sum * (1.0f / static_cast<float>(c.pointCount));
    float spread = 0.0f;
    for (const uint32_t i : members) {
        spread += (points[i].pos - c.centroid).normSq();
    }
    c.radius = std::sqrt(spread / static_cast<float>(c.pointCount));
    c.members = std::move(members);
    return c;
}

} // namespace

std::vector<Cluster> clusterPoints(const std::vector<WorldPoint>& points,
                                   const ClusteringParams& params) {
    // Guard a non-positive link distance (hand-edited config): it is the cell
    // size of the spatial hash, and 0 would divide by zero in the grid map.
    const float cell = params.linkDistance > 1e-4f ? params.linkDistance : 0.35f;
    const float linkSq = cell * cell;
    const size_t n = points.size();

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
        label[seed] = 1;
        stack.assign(1, seed);
        std::vector<uint32_t> members;

        while (!stack.empty()) {
            const uint32_t i = stack.back();
            stack.pop_back();
            members.push_back(i);
            const Vec2 p = points[i].pos;

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
                            label[j] = 1;
                            stack.push_back(j);
                        }
                    }
                }
            }
        }

        if (members.size() >= params.minPoints) {
            clusters.push_back(makeCluster(points, std::move(members)));
        }
    }
    return clusters;
}

namespace {

// Seeded k-means over one cluster's points. Returns the parts, or empty if a
// part falls below minPointsPerPart (split unsupported by the data).
std::vector<std::vector<uint32_t>> seededKmeans(const Cluster& cluster,
                                                const std::vector<WorldPoint>& points,
                                                std::vector<Vec2> seeds,
                                                const SplitParams& params) {
    const size_t k = seeds.size();
    std::vector<uint32_t> assign(cluster.members.size(), 0);
    for (uint32_t iter = 0; iter < params.kmeansIterations; ++iter) {
        for (size_t m = 0; m < cluster.members.size(); ++m) {
            const Vec2 p = points[cluster.members[m]].pos;
            float bestD = std::numeric_limits<float>::infinity();
            for (size_t s = 0; s < k; ++s) {
                const float d = (p - seeds[s]).normSq();
                if (d < bestD) {
                    bestD = d;
                    assign[m] = static_cast<uint32_t>(s);
                }
            }
        }
        std::vector<Vec2> sums(k, Vec2{});
        std::vector<uint32_t> counts(k, 0);
        for (size_t m = 0; m < cluster.members.size(); ++m) {
            sums[assign[m]] = sums[assign[m]] + points[cluster.members[m]].pos;
            counts[assign[m]]++;
        }
        for (size_t s = 0; s < k; ++s) {
            if (counts[s] > 0) {
                seeds[s] = sums[s] * (1.0f / static_cast<float>(counts[s]));
            }
        }
    }
    std::vector<std::vector<uint32_t>> parts(k);
    for (size_t m = 0; m < cluster.members.size(); ++m) {
        parts[assign[m]].push_back(cluster.members[m]);
    }
    for (const std::vector<uint32_t>& part : parts) {
        if (part.size() < params.minPointsPerPart) {
            return {};
        }
    }
    return parts;
}

} // namespace

std::vector<Cluster> splitMergedClusters(std::vector<Cluster> clusters,
                                         const std::vector<WorldPoint>& points,
                                         const std::vector<Vec2>& predictions,
                                         const SplitParams& params) {
    std::vector<Cluster> out;
    out.reserve(clusters.size());

    for (Cluster& cluster : clusters) {
        // Which predicted tracks claim this cluster's footprint?
        const float capture = cluster.radius + params.captureMargin;
        const float captureSq = capture * capture;
        std::vector<Vec2> seeds;
        for (const Vec2 p : predictions) {
            if ((p - cluster.centroid).normSq() <= captureSq) {
                seeds.push_back(p);
            }
        }

        if (seeds.size() >= 2 && cluster.radius > params.splitMinRadius &&
            cluster.pointCount >= seeds.size() * params.minPointsPerPart) {
            // Prediction-guided split: parts feed the claiming tracks only.
            // The split must be supported by the data: if the resulting parts
            // are not clearly separated, this is one physical lump (full
            // overlap) and cutting it would feed each filter an arbitrary
            // half — reject and let the tracker treat it as shared.
            auto parts = seededKmeans(cluster, points, seeds, params);
            if (!parts.empty()) {
                std::vector<Cluster> cut;
                cut.reserve(parts.size());
                for (std::vector<uint32_t>& part : parts) {
                    cut.push_back(makeCluster(points, std::move(part)));
                }
                // Real lumps must be separated relative to their own size: a
                // k-means cut through one physical lump yields parts roughly
                // one part-diameter apart — that is the self-fulfilling cut
                // that swaps identities, and it must be rejected.
                bool separated = true;
                for (size_t a = 0; a < cut.size() && separated; ++a) {
                    for (size_t b = a + 1; b < cut.size(); ++b) {
                        const float required =
                            std::max(params.minSeparation,
                                     2.0f * std::max(cut[a].radius, cut[b].radius) + 0.10f);
                        if ((cut[a].centroid - cut[b].centroid).norm() < required) {
                            separated = false;
                            break;
                        }
                    }
                }
                if (separated) {
                    for (Cluster& c : cut) {
                        c.fromPredictionSplit = true;
                        out.push_back(std::move(c));
                    }
                    continue;
                }
            }
        } else if (cluster.radius > params.oversizeRadius &&
                   cluster.pointCount >= 2 * params.minPointsPerPart) {
            // Bootstrap split: bigger than one person, no predictions claim it
            // — probably several people entering together. Seed with the
            // farthest pair of member points.
            size_t bestA = 0, bestB = 0;
            float bestD = -1.0f;
            for (size_t a = 0; a < cluster.members.size(); ++a) {
                for (size_t b = a + 1; b < cluster.members.size(); ++b) {
                    const float d =
                        (points[cluster.members[a]].pos - points[cluster.members[b]].pos).normSq();
                    if (d > bestD) {
                        bestD = d;
                        bestA = a;
                        bestB = b;
                    }
                }
            }
            auto parts = seededKmeans(cluster, points,
                                      {points[cluster.members[bestA]].pos,
                                       points[cluster.members[bestB]].pos},
                                      params);
            if (!parts.empty()) {
                Cluster c0 = makeCluster(points, std::move(parts[0]));
                Cluster c1 = makeCluster(points, std::move(parts[1]));
                if ((c0.centroid - c1.centroid).norm() >= params.minSeparation) {
                    out.push_back(std::move(c0));
                    out.push_back(std::move(c1));
                    continue;
                }
            }
        }
        out.push_back(std::move(cluster));
    }
    return out;
}

} // namespace sillage
