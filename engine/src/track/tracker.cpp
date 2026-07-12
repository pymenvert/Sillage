#include "track/tracker.h"

#include <algorithm>
#include <cmath>
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

std::vector<Track> Tracker::update(const std::vector<Cluster>& clusters, float dt,
                                   uint64_t tick) {
    // 1. Predict.
    for (InternalTrack& t : tracks_) {
        t.filter.predict(dt);
    }

    // 2. Gated cost matrix: Mahalanobis distance + velocity-consistency term.
    const int rows = static_cast<int>(tracks_.size());
    const int cols = static_cast<int>(clusters.size());
    std::vector<float> cost(static_cast<size_t>(rows) * cols, kForbidden);
    for (int r = 0; r < rows; ++r) {
        const InternalTrack& t = tracks_[r];
        for (int c = 0; c < cols; ++c) {
            const float dm2 = t.filter.gateDistanceSq(clusters[c].centroid);
            if (dm2 > params_.gateChi2) {
                continue;
            }
            const Vec2 implied = (clusters[c].centroid - t.filter.position()) * (1.0f / dt);
            const float velMismatch = (implied - t.filter.velocity()).norm();
            cost[static_cast<size_t>(r) * cols + c] = dm2 + params_.velocityPenalty * velMismatch;
        }
    }

    const std::vector<int> assignment = solveAssignment(cost, rows, cols);

    // 3. Update matched tracks.
    std::vector<char> clusterUsed(clusters.size(), 0);
    for (int r = 0; r < rows; ++r) {
        InternalTrack& t = tracks_[r];
        const int c = assignment.empty() ? -1 : assignment[r];
        if (c >= 0) {
            clusterUsed[c] = 1;
            t.filter.update(clusters[c].centroid);
            t.radius = 0.8f * t.radius + 0.2f * clusters[c].radius;
            t.hits++;
            t.consecutiveMisses = 0;
            if (!t.confirmed && t.hits >= params_.confirmHits) {
                t.confirmed = true;
            }
        } else {
            t.consecutiveMisses++;
        }
    }

    // 4. Kill lost tracks.
    std::erase_if(tracks_, [this](const InternalTrack& t) {
        if (!t.confirmed) {
            return t.consecutiveMisses > params_.tentativeMaxMiss;
        }
        return t.consecutiveMisses > params_.coastMaxTicks;
    });

    // 5. Births from unclaimed clusters.
    for (size_t c = 0; c < clusters.size(); ++c) {
        if (clusterUsed[c]) {
            continue;
        }
        InternalTrack t;
        t.id = nextId_++;
        t.bornTick = tick;
        t.filter = KalmanCV(clusters[c].centroid, params_.kalman);
        t.radius = clusters[c].radius;
        t.hits = 1;
        tracks_.push_back(std::move(t));
    }

    // 6. Publish confirmed tracks, oid = rank by id among the published set.
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
