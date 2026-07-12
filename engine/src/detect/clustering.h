#pragma once

#include "core/types.h"

#include <vector>

namespace sillage {

struct ClusteringParams {
    float linkDistance = 0.35f; // meters; two legs of one person link, two people don't
    uint32_t minPoints = 3;     // clusters below this are noise
};

// Euclidean clustering on a spatial hash grid. O(n) for lidar-sized inputs.
std::vector<Cluster> clusterPoints(const std::vector<WorldPoint>& points,
                                   const ClusteringParams& params);

} // namespace sillage
