#pragma once

#include "core/background.h"
#include "core/types.h"
#include "detect/clustering.h"
#include "track/tracker.h"

#include <vector>

namespace sillage {

struct PipelineConfig {
    std::vector<SensorPose> sensors;
    uint32_t backgroundBins = 720;
    uint32_t backgroundLearnFrames = 60;
    float backgroundMargin = 0.15f;
    ClusteringParams clustering{};
    SplitParams split{};
    TrackerParams tracker{};
};

// The processing chain from raw scans to published tracks:
// background subtraction (per sensor) -> room-frame fusion -> clustering ->
// track-aware splitting -> tracking. Deterministic: same frames in, same
// snapshot out. Used identically by the live engine and the eval harness.
class Pipeline {
public:
    explicit Pipeline(PipelineConfig config);

    // True while the background models are still learning (empty room phase).
    bool learning() const;

    FrameSnapshot process(const std::vector<ScanFrame>& frames, float dt, uint64_t tick,
                          Vec2 roomSize);

private:
    PipelineConfig config_;
    std::vector<BackgroundModel> backgrounds_;
    Tracker tracker_;
};

} // namespace sillage
