#pragma once

#include "core/background.h"
#include "core/types.h"
#include "detect/clustering.h"
#include "track/tracker.h"

#include <cstdint>
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

    // True while EVERY sensor is still learning, i.e. nothing is trackable yet.
    // Deliberately not "any sensor is learning": a lidar that never connects
    // must not gag the whole engine — the ones that are ready keep tracking.
    bool learning() const;

    // True for a sensor that has not finished learning its background. A sensor
    // that never delivered a frame stays here forever, so callers can tell
    // "empty room, please wait" apart from "sensor 2 is offline".
    bool sensorLearning(SensorId sensor) const;

    // Discards the learned background so the next frames rebuild it. Needed on
    // site: an engine restarted with the audience already seated has learned
    // the audience as background and would see nobody.
    void relearnBackground();

    FrameSnapshot process(const std::vector<ScanFrame>& frames, float dt, uint64_t tick,
                          Vec2 roomSize);

private:
    PipelineConfig config_;
    std::vector<BackgroundModel> backgrounds_;
    Tracker tracker_;
};

} // namespace sillage
