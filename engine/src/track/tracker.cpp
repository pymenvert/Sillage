#include "track/tracker.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace sillage {

namespace {
constexpr float kForbidden = 1e8f;
constexpr float kDummy = 1e6f; // padding columns; beating this means a real, gated match
} // namespace

// Hungarian algorithm with potentials (Jonker-Volgenant flavored), squared cost
// matrix padded so every row can stay unassigned via a dummy column.
std::vector<int> solveAssignment(const std::vector<float>& cost, int rows, int cols) {
    if (rows == 0) {
        return {};
    }
    const int n = std::max(rows, cols);
    auto at = [&](int r, int c) -> float {
        if (r < rows && c < cols) {
            return cost[static_cast<size_t>(r) * cols + c];
        }
        return kDummy;
    };

    constexpr double inf = std::numeric_limits<double>::infinity();
    std::vector<double> u(n + 1, 0.0), v(n + 1, 0.0);
    std::vector<int> p(n + 1, 0), way(n + 1, 0);

    for (int i = 1; i <= n; ++i) {
        p[0] = i;
        int j0 = 0;
        std::vector<double> minv(n + 1, inf);
        std::vector<char> used(n + 1, 0);
        do {
            used[j0] = 1;
            const int i0 = p[j0];
            double delta = inf;
            int j1 = 0;
            for (int j = 1; j <= n; ++j) {
                if (used[j]) {
                    continue;
                }
                const double cur = at(i0 - 1, j - 1) - u[i0] - v[j];
                if (cur < minv[j]) {
                    minv[j] = cur;
                    way[j] = j0;
                }
                if (minv[j] < delta) {
                    delta = minv[j];
                    j1 = j;
                }
            }
            for (int j = 0; j <= n; ++j) {
                if (used[j]) {
                    u[p[j]] += delta;
                    v[j] -= delta;
                } else {
                    minv[j] -= delta;
                }
            }
            j0 = j1;
        } while (p[j0] != 0);
        do {
            const int j1 = way[j0];
            p[j0] = p[j1];
            j0 = j1;
        } while (j0 != 0);
    }

    std::vector<int> assignment(rows, -1);
    for (int j = 1; j <= n; ++j) {
        const int i = p[j];
        if (i >= 1 && i <= rows && j <= cols && at(i - 1, j - 1) < kDummy) {
            assignment[i - 1] = j - 1;
        }
    }
    return assignment;
}

std::vector<Vec2> Tracker::beginTick(float dt) {
    if (!(dt > 0.0f)) {
        dt = 1.0f / 60.0f;
    }
    std::vector<Vec2> predictions;
    for (InternalTrack& t : tracks_) {
        t.filter.predict(dt);
        if (t.confirmed) {
            predictions.push_back(t.filter.position());
        }
    }
    return predictions;
}

void Tracker::associate(const std::vector<int>& trackIdx, const std::vector<int>& clusterIdx,
                        const std::vector<Cluster>& clusters, float dt, uint64_t tick,
                        std::vector<int>& trackToCluster) const {
    const int rows = static_cast<int>(trackIdx.size());
    const int cols = static_cast<int>(clusterIdx.size());
    if (rows == 0 || cols == 0) {
        return;
    }
    std::vector<float> cost(static_cast<size_t>(rows) * cols, kForbidden);
    for (int r = 0; r < rows; ++r) {
        const InternalTrack& t = tracks_[trackIdx[r]];
        const float gate =
            params_.gateChi2 *
            std::min(1.0f + params_.gateMissGrowth * static_cast<float>(t.consecutiveMisses),
                     params_.gateMissCap);
        for (int c = 0; c < cols; ++c) {
            const Cluster& cluster = clusters[clusterIdx[c]];
            const float dm2 = t.filter.gateDistanceSq(cluster.centroid);
            if (dm2 > gate) {
                continue;
            }
            const Vec2 implied = (cluster.centroid - t.filter.position()) * (1.0f / dt);
            const float velMismatch = (implied - t.filter.velocity()).norm();
            // Staleness counts from the last real measurement (NOT the miss
            // counter, which shared-measurement freezing keeps at zero).
            const auto stale = static_cast<float>(tick - t.lastMeasuredTick);
            const float continuity = (cluster.centroid - t.lastMeasuredPos).normSq() *
                                     params_.continuityWeight / (1.0f + stale);
            cost[static_cast<size_t>(r) * cols + c] =
                dm2 + params_.velocityPenalty * velMismatch + continuity;
        }
    }
    const std::vector<int> result = solveAssignment(cost, rows, cols);
    for (int r = 0; r < rows; ++r) {
        if (result[r] >= 0) {
            trackToCluster[trackIdx[r]] = clusterIdx[result[r]];
        }
    }
}

std::vector<Track> Tracker::commit(const std::vector<Cluster>& clusters, float dt,
                                   uint64_t tick) {
    // Guard a non-positive dt: velocity terms divide by it. dt comes from a
    // fixed tick rate today, but a future variable-rate source with a zero
    // interval would inject inf/NaN into the cost matrix and outputs.
    if (!(dt > 0.0f)) {
        dt = 1.0f / 60.0f;
    }
    // Shared measurements: full-overlap blobs claimed by several confirmed
    // tracks feed no filter; their claimants coast without aging.
    std::vector<char> clusterShared(clusters.size(), 0);
    std::vector<char> trackFrozen(tracks_.size(), 0);
    for (int c = 0; c < static_cast<int>(clusters.size()); ++c) {
        const Cluster& cluster = clusters[c];
        if (cluster.fromPredictionSplit) {
            continue;
        }
        const float capture = cluster.radius + params_.sharedCaptureMargin;
        std::vector<size_t> claimants;
        for (size_t i = 0; i < tracks_.size(); ++i) {
            if (tracks_[i].confirmed &&
                (tracks_[i].filter.position() - cluster.centroid).norm() <= capture) {
                claimants.push_back(i);
            }
        }
        if (claimants.size() >= 2) {
            clusterShared[c] = 1;
            for (const size_t i : claimants) {
                trackFrozen[i] = 1;
            }
        }
    }

    // Partition clusters by strength (ByteTrack-inspired two-pass, docs/03 §5).
    std::vector<int> strongClusters, weakClusters;
    for (int c = 0; c < static_cast<int>(clusters.size()); ++c) {
        if (clusterShared[c]) {
            continue;
        }
        (clusters[c].pointCount >= params_.weakClusterPoints ? strongClusters : weakClusters)
            .push_back(c);
    }

    std::vector<int> trackToCluster(tracks_.size(), -1);

    // Association hierarchy: confirmed identities are the asset being
    // protected, so they pick first; tentatives only get what is left. A
    // tentative must never be able to steal a cluster from a confirmed track
    // that is briefly out of shape (sharp turn, partial occlusion).
    std::vector<int> confirmedTracks, tentativeTracks;
    for (size_t i = 0; i < tracks_.size(); ++i) {
        (tracks_[i].confirmed ? confirmedTracks : tentativeTracks).push_back(static_cast<int>(i));
    }

    // Pass 1: confirmed tracks versus strong clusters.
    associate(confirmedTracks, strongClusters, clusters, dt, tick, trackToCluster);

    // Pass 2: still-unmatched confirmed tracks versus weak clusters — the
    // faint half-occluded measurement is what keeps an id alive.
    std::vector<int> unmatchedConfirmed;
    for (const int i : confirmedTracks) {
        if (trackToCluster[i] < 0) {
            unmatchedConfirmed.push_back(i);
        }
    }
    associate(unmatchedConfirmed, weakClusters, clusters, dt, tick, trackToCluster);

    // Pass 3: tentative tracks versus the strong clusters nobody claimed.
    std::vector<char> claimed(clusters.size(), 0);
    for (const int c : trackToCluster) {
        if (c >= 0) {
            claimed[c] = 1;
        }
    }
    std::vector<int> freeStrong;
    for (const int c : strongClusters) {
        if (!claimed[c]) {
            freeStrong.push_back(c);
        }
    }
    associate(tentativeTracks, freeStrong, clusters, dt, tick, trackToCluster);

    // Update matched tracks.
    std::vector<char> clusterUsed(clusters.size(), 0);
    for (size_t i = 0; i < tracks_.size(); ++i) {
        InternalTrack& t = tracks_[i];
        const int c = trackToCluster[i];
        if (c >= 0) {
            clusterUsed[c] = 1;
            t.filter.update(clusters[c].centroid);
            t.radius = 0.8f * t.radius + 0.2f * clusters[c].radius;
            t.hits++;
            t.consecutiveMisses = 0;
            t.lastMeasuredPos = t.filter.position();
            t.lastMeasuredVel = t.filter.velocity();
            t.lastMeasuredTick = tick;
            if (!t.confirmed && t.hits >= params_.confirmHits) {
                t.confirmed = true;
            }
        } else if (!trackFrozen[i]) {
            t.consecutiveMisses++;
        }
    }

    // Duplicate suppression: a coasting track glued to another track that IS
    // being measured is a zombie (two ids on one person), not an occlusion.
    // It dies quickly and leaves no grave — the person still has its id.
    std::vector<char> duplicate(tracks_.size(), 0);
    for (size_t i = 0; i < tracks_.size(); ++i) {
        const InternalTrack& t = tracks_[i];
        if (!t.confirmed || t.consecutiveMisses < params_.duplicateKillMisses) {
            continue;
        }
        for (size_t j = 0; j < tracks_.size(); ++j) {
            if (j == i || !tracks_[j].confirmed || trackToCluster[j] < 0) {
                continue;
            }
            if ((t.filter.position() - tracks_[j].filter.position()).norm() < 0.35f) {
                duplicate[i] = 1;
                break;
            }
        }
    }
    size_t idx = 0;
    std::erase_if(tracks_, [&](const InternalTrack& t) {
        const bool isDuplicate = duplicate[idx++] != 0;
        if (isDuplicate) {
            return true; // no grave: the physical person kept the other id
        }
        if (!t.confirmed) {
            return t.consecutiveMisses > params_.tentativeMaxMiss;
        }
        if (t.consecutiveMisses > params_.coastMaxTicks) {
            graves_.push_back({t.id, t.bornTick, t.lastMeasuredPos, t.lastMeasuredVel, t.radius,
                               t.lastMeasuredTick});
            return true;
        }
        return false;
    });
    std::erase_if(graves_, [&](const Grave& g) {
        return tick - g.deathTick > params_.graveTicks;
    });

    // Births from unclaimed strong clusters; a nearby grave passes its id on.
    // Parts manufactured by prediction-guided splitting never spawn tracks.
    for (const int c : strongClusters) {
        if (clusterUsed[c] || clusters[c].fromPredictionSplit) {
            continue;
        }
        const Cluster& cluster = clusters[c];
        InternalTrack t;
        t.filter = KalmanCV(cluster.centroid, params_.kalman);
        t.radius = cluster.radius;
        t.hits = 1;
        t.bornTick = tick;
        t.lastMeasuredPos = cluster.centroid;
        t.lastMeasuredTick = tick;

        // Grave lookup: position must be reachable from where the id died.
        int bestGrave = -1;
        float bestDist = std::numeric_limits<float>::infinity();
        for (int g = 0; g < static_cast<int>(graves_.size()); ++g) {
            const Grave& grave = graves_[g];
            const float elapsed = static_cast<float>(tick - grave.deathTick) * dt;
            const Vec2 expected = grave.position + grave.velocity * elapsed;
            const float allowance =
                std::min(params_.reidBaseRadius + params_.reidGrowthPerSec * elapsed,
                         params_.reidMaxRadius);
            const float d = (cluster.centroid - expected).norm();
            if (d <= allowance &&
                std::abs(cluster.radius - grave.radius) <= params_.reidRadiusTolerance &&
                d < bestDist) {
                bestDist = d;
                bestGrave = g;
            }
        }
        if (bestGrave >= 0) {
            t.id = graves_[bestGrave].id;
            t.bornTick = graves_[bestGrave].bornTick; // age continuity
            t.confirmed = true;                       // it is a continuation, not a birth
            graves_.erase(graves_.begin() + bestGrave);
        } else {
            t.id = nextId_++;
        }
        tracks_.push_back(std::move(t));
    }

    // Publish confirmed tracks, oid = rank by id among the published set.
    std::vector<Track> out;
    out.reserve(tracks_.size());
    for (const InternalTrack& t : tracks_) {
        if (!t.confirmed) {
            continue;
        }
        Track pub;
        pub.id = t.id;
        pub.bornTick = t.bornTick;
        pub.position = t.filter.position();
        pub.velocity = t.filter.velocity();
        pub.radius = t.radius;
        pub.state = t.consecutiveMisses > 0 ? TrackState::Coasting : TrackState::Confirmed;
        pub.confidence = 1.0f - static_cast<float>(t.consecutiveMisses) /
                                    static_cast<float>(params_.coastMaxTicks + 1);
        out.push_back(pub);
    }
    std::sort(out.begin(), out.end(), [](const Track& a, const Track& b) { return a.id < b.id; });
    for (size_t i = 0; i < out.size(); ++i) {
        out[i].oid = static_cast<uint32_t>(i);
    }
    return out;
}

} // namespace sillage
