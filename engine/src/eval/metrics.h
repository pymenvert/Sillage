#pragma once

#include "core/types.h"

#include <map>
#include <set>
#include <vector>

namespace sillage {

// Ground truth for one frame: live agent index + position.
struct GtPoint {
    uint32_t agent = 0;
    Vec2 position{};
};

struct MotResult {
    int idSwitches = 0;
    int misses = 0;
    int falsePositives = 0;
    int totalGt = 0;         // sum over frames of live ground-truth agents
    int totalTrackFrames = 0; // sum over frames of published tracks
    float mota = 0.0f;       // 1 - (misses + fp + switches) / totalGt
    float idf1 = 0.0f;       // identity F1 with globally optimal gt<->id mapping
    int distinctIds = 0;     // how many track ids were ever published
    int maxSimultaneous = 0; // peak published tracks in one frame
};

// Standard MOT accounting against simulator ground truth. Per-frame matching
// is a gated optimal assignment on distance; identity bookkeeping follows the
// CLEAR-MOT / IDF1 definitions closely enough to gate CI on.
class MotAccumulator {
public:
    explicit MotAccumulator(float gateMeters = 0.6f) : gate_(gateMeters) {}

    void addFrame(const std::vector<GtPoint>& groundTruth, const std::vector<Track>& tracks);
    MotResult result() const;

private:
    float gate_;
    int idSwitches_ = 0;
    int misses_ = 0;
    int falsePositives_ = 0;
    int totalGt_ = 0;
    int totalTrackFrames_ = 0;
    int maxSimultaneous_ = 0;
    std::set<uint32_t> seenIds_;
    std::map<uint32_t, uint32_t> lastMatchedId_;             // agent -> last track id
    std::map<std::pair<uint32_t, uint32_t>, int> overlap_;   // (agent, track id) -> frames
};

} // namespace sillage
