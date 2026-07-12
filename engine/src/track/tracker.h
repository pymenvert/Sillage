#pragma once

#include "core/types.h"
#include "track/kalman.h"

#include <vector>

namespace sillage {

struct TrackerParams {
    KalmanCV::Params kalman{};
    float gateChi2 = 9.21f;        // 99% for 2 dof — association gate
    uint32_t confirmHits = 3;      // hits needed to leave probation
    uint32_t tentativeMaxMiss = 2; // consecutive misses killing a tentative track
    uint32_t coastMaxTicks = 60;   // ticks a confirmed track survives unmeasured (1s @60Hz)
    float velocityPenalty = 0.5f;  // weight of velocity-consistency term in the cost
};

// Multi-object tracker: Kalman per track, globally optimal assignment
// (Hungarian on the gated cost matrix), M/N probation, coasting.
// M1 adds: track-aware cluster splitting, two-pass weak-cluster association,
// anti-swap hypothesis test, re-identification graves (docs/03).
class Tracker {
public:
    explicit Tracker(const TrackerParams& params) : params_(params) {}

    // Advances one tick and returns the published set (confirmed + coasting).
    std::vector<Track> update(const std::vector<Cluster>& clusters, float dt, uint64_t tick);

private:
    struct InternalTrack {
        uint32_t id = 0;
        uint64_t bornTick = 0;
        KalmanCV filter;
        float radius = 0.0f;
        uint32_t hits = 0;
        uint32_t consecutiveMisses = 0;
        bool confirmed = false;
    };

    TrackerParams params_;
    std::vector<InternalTrack> tracks_;
    uint32_t nextId_ = 1;
};

// Solves min-cost assignment on a dense cost matrix (rows -> cols, INF = forbidden).
// Returns for each row the assigned column or -1. O(n^3) Hungarian — trivial at n<=100.
std::vector<int> solveAssignment(const std::vector<float>& cost, int rows, int cols);

} // namespace sillage
