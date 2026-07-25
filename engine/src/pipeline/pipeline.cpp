#include "pipeline/pipeline.h"

#include <algorithm>
#include <cstdint>

namespace sillage {

Pipeline::Pipeline(PipelineConfig config)
    : config_(std::move(config)), tracker_(config_.tracker) {
    for (size_t i = 0; i < config_.sensors.size(); ++i) {
        backgrounds_.emplace_back(config_.backgroundBins, config_.backgroundLearnFrames,
                                  config_.backgroundMargin);
    }
}

bool Pipeline::learning() const {
    return std::any_of(backgrounds_.begin(), backgrounds_.end(),
                       [](const BackgroundModel& b) { return b.learning(); });
}

FrameSnapshot Pipeline::process(const std::vector<ScanFrame>& frames, float dt, uint64_t tick,
                                Vec2 roomSize) {
    FrameSnapshot snap;
    snap.tick = tick;
    snap.timeSeconds = static_cast<double>(tick) * static_cast<double>(dt);
    snap.roomSize = roomSize;

    bool anyLearning = false;
    for (const ScanFrame& frame : frames) {
        if (frame.sensor >= backgrounds_.size()) {
            continue;
        }
        BackgroundModel& bg = backgrounds_[frame.sensor];
        if (bg.learning()) {
            bg.learn(frame);
            anyLearning = true;
            continue;
        }
        const SensorPose& pose = config_.sensors[frame.sensor];
        for (const RangePoint& p : frame.points) {
            if (bg.isForeground(p)) {
                snap.foreground.push_back({pose.toRoom(p), frame.sensor});
            }
        }
    }
    if (anyLearning) {
        return snap;
    }

    const std::vector<Vec2> predictions = tracker_.beginTick(dt);
    snap.clusters = clusterPoints(snap.foreground, config_.clustering);
    snap.clusters =
        splitMergedClusters(std::move(snap.clusters), snap.foreground, predictions, config_.split);
    snap.tracks = tracker_.commit(snap.clusters, dt, tick);
    return snap;
}

} // namespace sillage
