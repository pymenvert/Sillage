#pragma once

#include "core/types.h"
#include "track/kalman.h"

#include <vector>

namespace sillage {

struct TrackerParams {
    KalmanCV::Params kalman{};
    float gateChi2 = 9.21f;        // 99% for 2 dof — association gate
    // The gate widens while a track misses (sharp turnarounds throw the
    // prediction out momentarily; the established identity must recapture its
    // measurement before a newborn usurps it), capped at gateMissCap x base.
    float gateMissGrowth = 0.5f;
    float gateMissCap = 4.0f;
    uint32_t confirmHits = 5;      // hits needed to leave probation
    uint32_t tentativeMaxMiss = 2; // consecutive misses killing a tentative track
    uint32_t coastMaxTicks = 120;  // ticks a confirmed track survives unmeasured (2s @60Hz)
    uint32_t duplicateKillMisses = 30; // coasting glued to a measured track dies after this
    float velocityPenalty = 0.5f;  // weight of velocity-consistency term in the cost
    // Position-continuity anchor: at 60 Hz a person moves ~2 cm per tick, so
    // the distance to the track's last *measured* position is the most
    // reliable tick-to-tick discriminator — constant-velocity predictions
    // cross each other during avoidance curves, this term does not. Decays
    // with misses (a stale anchor must not fight a coasting prediction).
    float continuityWeight = 100.0f; // cost per m^2 from last measured position

    // Two-pass association (docs/03 §5): clusters below this point count are
    // "weak" — matched only to already-established tracks in a second pass,
    // and never allowed to spawn a new track.
    uint32_t weakClusterPoints = 5;

    // Shared measurement (docs/03 §3 last resort): an unsplit cluster claimed
    // closely by two or more confirmed tracks (full overlap) feeds NO filter —
    // pulling one filter into the mixture centroid corrupts its velocity and
    // causes the swap at separation. The claiming tracks coast with their miss
    // counters frozen instead. The margin is tight on purpose: a neighbor
    // walking 40 cm away must not freeze anybody.
    float sharedCaptureMargin = 0.15f;

    // Re-identification graves (docs/03 §7). The window counts from the last
    // real measurement (which precedes death by the whole coasting phase).
    uint32_t graveTicks = 300;       // how long a dead id waits for its owner (5s @60Hz)
    float reidBaseRadius = 0.4f;     // meters of slack at the moment of death
    float reidGrowthPerSec = 0.7f;   // uncertainty growth of the grave's position
    float reidMaxRadius = 2.5f;
    float reidRadiusTolerance = 0.3f; // extent similarity required to inherit an id
};

// Multi-object tracker: Kalman per track, globally optimal assignment on a
// gated cost matrix, M/N probation, coasting, two-pass weak-cluster
// association, re-identification graves.
//
// Two-phase API so the detector can use predictions for track-aware cluster
// splitting between the phases:
//   auto predictions = tracker.beginTick(dt);
//   clusters = splitMergedClusters(clusters, points, predictions, ...);
//   tracks = tracker.commit(clusters, dt, tick);
class Tracker {
public:
    explicit Tracker(const TrackerParams& params) : params_(params) {}

    // Predicts all filters one tick ahead; returns predicted positions of
    // confirmed tracks (the ones cluster splitting should honor).
    std::vector<Vec2> beginTick(float dt);

    // Associates, updates, manages lifecycle. Returns the published set.
    std::vector<Track> commit(const std::vector<Cluster>& clusters, float dt, uint64_t tick);

    // Convenience for tests and simple callers.
    std::vector<Track> update(const std::vector<Cluster>& clusters, float dt, uint64_t tick) {
        beginTick(dt);
        return commit(clusters, dt, tick);
    }

private:
    struct InternalTrack {
        uint32_t id = 0;
        uint64_t bornTick = 0;
        KalmanCV filter;
        float radius = 0.0f;
        uint32_t hits = 0;
        uint32_t consecutiveMisses = 0;
        bool confirmed = false;
        // Last state backed by a real measurement — the grave anchor. A track
        // that coasted before dying drifted; re-identification must reason
        // from the last thing actually seen.
        Vec2 lastMeasuredPos{};
        Vec2 lastMeasuredVel{};
        uint64_t lastMeasuredTick = 0;
    };

    struct Grave {
        uint32_t id = 0;
        uint64_t bornTick = 0;
        Vec2 position{};
        Vec2 velocity{};
        float radius = 0.0f;
        uint64_t deathTick = 0;
    };

    // Assignment over a track-index subset and cluster-index subset.
    void associate(const std::vector<int>& trackIdx, const std::vector<int>& clusterIdx,
                   const std::vector<Cluster>& clusters, float dt, uint64_t tick,
                   std::vector<int>& trackToCluster) const;

    TrackerParams params_;
    std::vector<InternalTrack> tracks_;
    std::vector<Grave> graves_;
    uint32_t nextId_ = 1;
};

// Solves min-cost assignment on a dense cost matrix (rows -> cols, kForbidden
// entries stay unassigned). Returns for each row the assigned column or -1.
std::vector<int> solveAssignment(const std::vector<float>& cost, int rows, int cols);

} // namespace sillage
