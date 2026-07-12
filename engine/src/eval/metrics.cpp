#include "eval/metrics.h"

#include "track/tracker.h" // solveAssignment

#include <algorithm>
#include <set>

namespace sillage {

void MotAccumulator::addFrame(const std::vector<GtPoint>& groundTruth,
                              const std::vector<Track>& tracks) {
    const int rows = static_cast<int>(groundTruth.size());
    const int cols = static_cast<int>(tracks.size());
    totalGt_ += rows;
    totalTrackFrames_ += cols;
    maxSimultaneous_ = std::max(maxSimultaneous_, cols);
    for (const Track& t : tracks) {
        seenIds_.insert(t.id);
    }

    // CLEAR-MOT matching: existing agent<->track pairs persist while they stay
    // within the gate (otherwise two entities passing through each other's
    // gates would register phantom switches); only the remainder is re-matched
    // by gated optimal assignment on distance.
    std::set<int> matchedTracks;
    std::vector<char> agentMatched(rows, 0);
    for (int r = 0; r < rows; ++r) {
        const auto last = lastMatchedId_.find(groundTruth[r].agent);
        if (last == lastMatchedId_.end()) {
            continue;
        }
        for (int c = 0; c < cols; ++c) {
            if (tracks[c].id == last->second && !matchedTracks.contains(c) &&
                (groundTruth[r].position - tracks[c].position).norm() <= gate_) {
                matchedTracks.insert(c);
                agentMatched[r] = 1;
                ++overlap_[{groundTruth[r].agent, tracks[c].id}];
                break;
            }
        }
    }

    std::vector<int> freeAgents, freeTracks;
    for (int r = 0; r < rows; ++r) {
        if (!agentMatched[r]) {
            freeAgents.push_back(r);
        }
    }
    for (int c = 0; c < cols; ++c) {
        if (!matchedTracks.contains(c)) {
            freeTracks.push_back(c);
        }
    }
    const int fr = static_cast<int>(freeAgents.size());
    const int fc = static_cast<int>(freeTracks.size());
    if (fr > 0 && fc > 0) {
        std::vector<float> cost(static_cast<size_t>(fr) * fc, 1e8f);
        for (int r = 0; r < fr; ++r) {
            for (int c = 0; c < fc; ++c) {
                const float d =
                    (groundTruth[freeAgents[r]].position - tracks[freeTracks[c]].position).norm();
                if (d <= gate_) {
                    cost[static_cast<size_t>(r) * fc + c] = d;
                }
            }
        }
        const std::vector<int> assignment = solveAssignment(cost, fr, fc);
        for (int r = 0; r < fr; ++r) {
            if (assignment[r] < 0) {
                continue;
            }
            const int c = freeTracks[assignment[r]];
            matchedTracks.insert(c);
            agentMatched[freeAgents[r]] = 1;
            const uint32_t agent = groundTruth[freeAgents[r]].agent;
            const uint32_t trackId = tracks[c].id;
            const auto last = lastMatchedId_.find(agent);
            if (last != lastMatchedId_.end() && last->second != trackId) {
                ++idSwitches_;
            }
            lastMatchedId_[agent] = trackId;
            ++overlap_[{agent, trackId}];
        }
    }
    for (int r = 0; r < rows; ++r) {
        if (!agentMatched[r]) {
            ++misses_;
        }
    }
    falsePositives_ += cols - static_cast<int>(matchedTracks.size());
}

MotResult MotAccumulator::result() const {
    MotResult r;
    r.idSwitches = idSwitches_;
    r.misses = misses_;
    r.falsePositives = falsePositives_;
    r.totalGt = totalGt_;
    r.totalTrackFrames = totalTrackFrames_;
    r.mota = totalGt_ > 0
                 ? 1.0f - static_cast<float>(misses_ + falsePositives_ + idSwitches_) /
                              static_cast<float>(totalGt_)
                 : 0.0f;
    r.distinctIds = static_cast<int>(seenIds_.size());
    r.maxSimultaneous = maxSimultaneous_;

    // IDF1: globally optimal one-to-one mapping agent <-> track id maximizing
    // total co-occurrence, solved with the same assignment solver on negated
    // counts. IDTP = matched overlap; IDF1 = 2*IDTP / (totalGt + totalTracks).
    std::vector<uint32_t> agents;
    std::vector<uint32_t> ids;
    for (const auto& [key, count] : overlap_) {
        if (std::find(agents.begin(), agents.end(), key.first) == agents.end()) {
            agents.push_back(key.first);
        }
        if (std::find(ids.begin(), ids.end(), key.second) == ids.end()) {
            ids.push_back(key.second);
        }
    }
    const int rows = static_cast<int>(agents.size());
    const int cols = static_cast<int>(ids.size());
    int idtp = 0;
    if (rows > 0 && cols > 0) {
        int maxCount = 0;
        for (const auto& [key, count] : overlap_) {
            maxCount = std::max(maxCount, count);
        }
        std::vector<float> cost(static_cast<size_t>(rows) * cols,
                                static_cast<float>(maxCount) + 1.0f);
        for (const auto& [key, count] : overlap_) {
            const auto r0 = static_cast<size_t>(
                std::find(agents.begin(), agents.end(), key.first) - agents.begin());
            const auto c0 = static_cast<size_t>(
                std::find(ids.begin(), ids.end(), key.second) - ids.begin());
            cost[r0 * cols + c0] = static_cast<float>(maxCount - count);
        }
        const std::vector<int> mapping = solveAssignment(cost, rows, cols);
        for (int a = 0; a < rows; ++a) {
            if (mapping[a] >= 0) {
                const auto it = overlap_.find({agents[a], ids[mapping[a]]});
                if (it != overlap_.end()) {
                    idtp += it->second;
                }
            }
        }
    }
    const int denom = totalGt_ + totalTrackFrames_;
    r.idf1 = denom > 0 ? 2.0f * static_cast<float>(idtp) / static_cast<float>(denom) : 0.0f;
    return r;
}

} // namespace sillage
