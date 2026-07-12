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

struct SplitParams {
    float captureMargin = 0.30f;   // prediction within radius+margin claims the cluster
    uint32_t minPointsPerPart = 4; // abandon split if a part would fall below this
    uint32_t kmeansIterations = 3;
    // A cluster must be at least this big to plausibly hold two people —
    // prediction-guided splitting never cuts a single-person-sized cluster
    // (two tracks glued on one person must starve, not share it forever).
    // One person fully wrapped by sensors has RMS radius ~= body radius
    // (~0.18); a pair at social distance starts around 0.22.
    float splitMinRadius = 0.20f;
    // Bootstrap splitting: a cluster visibly larger than one person is re-cut
    // in two even without track predictions (two people entering side by side
    // would otherwise be born as a single track forever).
    float oversizeRadius = 0.26f;  // RMS radius beyond one human footprint
    float minSeparation = 0.28f;   // required distance between the two parts
};

// Track-aware splitting of merged clusters (docs/03 §3):
// - when k >= 2 predicted track positions fall inside one cluster's footprint,
//   the cluster is re-cut into k parts by k-means seeded at the predictions
//   (parts flagged fromPredictionSplit — they never spawn tracks);
// - clusters larger than a person with no claiming predictions are re-cut in
//   two by farthest-pair seeded k-means (parts may spawn tracks: bootstrap).
std::vector<Cluster> splitMergedClusters(std::vector<Cluster> clusters,
                                         const std::vector<WorldPoint>& points,
                                         const std::vector<Vec2>& predictions,
                                         const SplitParams& params);

} // namespace sillage
