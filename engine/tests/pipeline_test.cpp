#include "pipeline/pipeline.h"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <numbers>

namespace sillage {
namespace {

constexpr float kDt = 1.0f / 60.0f;
constexpr uint32_t kRays = 360;

// A scan of a flat wall, with an optional nearer arc standing in for a person.
ScanFrame wallScan(SensorId sensor, float wallM, bool withPerson = false) {
    ScanFrame f;
    f.sensor = sensor;
    for (uint32_t i = 0; i < kRays; ++i) {
        const float angle = 2.0f * std::numbers::pi_v<float> * static_cast<float>(i) /
                            static_cast<float>(kRays);
        const bool inPerson = withPerson && i >= 40 && i <= 52;
        f.points.push_back({angle, inPerson ? wallM * 0.4f : wallM});
    }
    return f;
}

PipelineConfig twoSensorConfig() {
    PipelineConfig cfg;
    cfg.sensors = {{{0.2f, 0.2f}, 0.0f}, {{9.8f, 7.8f}, 3.14159265f}};
    cfg.backgroundBins = kRays;
    cfg.backgroundLearnFrames = 5;
    return cfg;
}

// The show-stopper this guards against: a lidar that never connects (bad
// cable, wrong IP, slow PoE switch) used to leave the engine "learning"
// forever, emitting nothing at all for the entire show.
TEST(Pipeline, OneMissingSensorDoesNotGagTheEngine) {
    Pipeline pipeline(twoSensorConfig());
    const Vec2 room{10.0f, 8.0f};

    // Only sensor 0 ever delivers frames; sensor 1 is dead on arrival.
    for (uint64_t tick = 0; tick < 10; ++tick) {
        pipeline.process({wallScan(0, 4.0f)}, kDt, tick, room);
    }

    EXPECT_FALSE(pipeline.learning()) << "a ready sensor must unblock the engine";
    EXPECT_TRUE(pipeline.sensorLearning(1)) << "the dead sensor stays flagged, for the UI";

    // And it actually tracks on the sensor that works.
    FrameSnapshot snap;
    for (uint64_t tick = 10; tick < 30; ++tick) {
        snap = pipeline.process({wallScan(0, 4.0f, /*withPerson=*/true)}, kDt, tick, room);
    }
    EXPECT_FALSE(snap.foreground.empty()) << "foreground must flow from the live sensor";
}

TEST(Pipeline, StaysLearningUntilAtLeastOneSensorIsReady) {
    Pipeline pipeline(twoSensorConfig());
    EXPECT_TRUE(pipeline.learning());
    pipeline.process({wallScan(0, 4.0f)}, kDt, 0, {10.0f, 8.0f});
    EXPECT_TRUE(pipeline.learning()) << "one frame is not a learned background";
}

// Restarting with the audience already seated bakes the audience into the
// background; without a re-learn the room would look permanently empty.
TEST(Pipeline, RelearnBackgroundRebuildsFromScratch) {
    Pipeline pipeline(twoSensorConfig());
    const Vec2 room{10.0f, 8.0f};

    // Learn a background that (wrongly) contains the person.
    for (uint64_t tick = 0; tick < 10; ++tick) {
        pipeline.process({wallScan(0, 4.0f, /*withPerson=*/true)}, kDt, tick, room);
    }
    ASSERT_FALSE(pipeline.learning());
    const auto baked = pipeline.process({wallScan(0, 4.0f, true)}, kDt, 10, room);
    EXPECT_TRUE(baked.foreground.empty()) << "person baked into background: invisible";

    pipeline.requestRelearn();
    pipeline.process({wallScan(0, 4.0f)}, kDt, 11, room);
    EXPECT_TRUE(pipeline.learning()) << "relearn must reopen the learning phase";

    // Re-learn on an empty room, then the person is visible again.
    for (uint64_t tick = 12; tick < 21; ++tick) {
        pipeline.process({wallScan(0, 4.0f)}, kDt, tick, room);
    }
    ASSERT_FALSE(pipeline.learning());
    const auto after = pipeline.process({wallScan(0, 4.0f, true)}, kDt, 21, room);
    EXPECT_FALSE(after.foreground.empty()) << "person must be detected after re-learn";
}

// POST /api/background/relearn arrives on a connection thread while the tick
// thread is reading the background models. The request must therefore only be
// recorded, and acted on at the top of the next process() — never applied
// underneath a running tick.
TEST(Pipeline, RelearnRequestIsAppliedAtTheNextTickOnly) {
    Pipeline pipeline(twoSensorConfig());
    const Vec2 room{10.0f, 8.0f};

    for (uint64_t tick = 0; tick < 10; ++tick) {
        pipeline.process({wallScan(0, 4.0f)}, kDt, tick, room);
    }
    ASSERT_FALSE(pipeline.learning());

    pipeline.requestRelearn();
    EXPECT_FALSE(pipeline.learning()) << "the request alone must not touch the models";

    pipeline.process({wallScan(0, 4.0f)}, kDt, 10, room);
    EXPECT_TRUE(pipeline.learning()) << "the next tick consumes the request";
}

// A double-click on the re-learn button must cost one reset, not two: the
// second must not reopen a learning phase that the first already completed.
TEST(Pipeline, RepeatedRelearnRequestsCollapseIntoOne) {
    Pipeline pipeline(twoSensorConfig());
    const Vec2 room{10.0f, 8.0f};

    for (uint64_t tick = 0; tick < 10; ++tick) {
        pipeline.process({wallScan(0, 4.0f)}, kDt, tick, room);
    }
    ASSERT_FALSE(pipeline.learning());

    // Exactly backgroundLearnFrames ticks: just enough for one learning cycle,
    // so a second reset consumed on any of them would leave the sensor short
    // of a frame and still learning.
    pipeline.requestRelearn();
    pipeline.requestRelearn();
    for (uint64_t tick = 10; tick < 15; ++tick) {
        pipeline.process({wallScan(0, 4.0f)}, kDt, tick, room);
    }
    EXPECT_FALSE(pipeline.learning()) << "a second reset would have reopened learning";
}

} // namespace
} // namespace sillage
