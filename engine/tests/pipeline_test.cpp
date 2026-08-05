#include "core/frame_hold.h"
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

// The pose hot-apply contract behind calibration: the background is learned
// in each sensor's own polar frame, so replacing a pose must not reopen the
// learning phase — and the same scan must fuse through the NEW pose. If a
// pose change forced a re-learn, calibration would always end with "restart
// the engine", i.e. re-learn the background in a room no longer empty.
TEST(Pipeline, MovingASensorKeepsItsLearnedBackground) {
    PipelineConfig cfg = twoSensorConfig();
    Pipeline pipeline(cfg);
    const Vec2 room{10.0f, 8.0f};

    for (uint64_t tick = 0; tick < 10; ++tick) {
        pipeline.process({wallScan(0, 4.0f)}, kDt, tick, room);
    }
    ASSERT_FALSE(pipeline.learning());
    const auto before = pipeline.process({wallScan(0, 4.0f, /*withPerson=*/true)}, kDt, 10, room);
    ASSERT_FALSE(before.foreground.empty());

    // Move sensor 0 by +1 m in x. Same scan: still no re-learn, and every
    // foreground point lands exactly 1 m to the right of where it did.
    auto poses = cfg.sensors;
    poses[0].position.x += 1.0f;
    ASSERT_TRUE(pipeline.setSensorPoses(poses));
    EXPECT_FALSE(pipeline.learning()) << "a pose change must never reopen learning";

    const auto after = pipeline.process({wallScan(0, 4.0f, true)}, kDt, 11, room);
    ASSERT_EQ(after.foreground.size(), before.foreground.size());
    for (size_t i = 0; i < after.foreground.size(); ++i) {
        EXPECT_NEAR(after.foreground[i].pos.x, before.foreground[i].pos.x + 1.0f, 1e-5f);
        EXPECT_NEAR(after.foreground[i].pos.y, before.foreground[i].pos.y, 1e-5f);
    }

    // Adding/removing a sensor is a wiring change, not a pose change.
    poses.push_back({{5.0f, 5.0f}, 0.0f});
    EXPECT_FALSE(pipeline.setSensorPoses(poses));
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

// --- FrameHold: slow sensors contribute every tick, bounded by age ------------

ScanFrame frameFor(SensorId sensor, float distance) {
    ScanFrame f;
    f.sensor = sensor;
    f.points = {{0.0f, distance}};
    return f;
}

// A 15 Hz sensor against a 60 Hz tick delivers one tick out of four. The
// three empty ticks must re-present its last scan: without this, detections
// arrive with 3-tick gaps and tentativeMaxMiss (2) kills every probationary
// track before its confirmHits-th (5) hit — a room covered by slow sensors
// alone tracks nobody.
TEST(FrameHold, RepresentsTheLastScanBetweenRevolutions) {
    FrameHold hold(0.25f);
    const float dt = 1.0f / 60.0f;

    std::vector<ScanFrame> frames = {frameFor(0, 2.0f)};
    hold.augment(frames, 0, dt);
    ASSERT_EQ(frames.size(), 1u) << "a fresh frame is never duplicated";

    for (uint64_t tick = 1; tick <= 3; ++tick) {
        frames.clear();
        hold.augment(frames, tick, dt);
        ASSERT_EQ(frames.size(), 1u) << "tick " << tick;
        EXPECT_EQ(frames[0].sensor, 0u);
        EXPECT_FLOAT_EQ(frames[0].points[0].distance, 2.0f);
    }

    // The next revolution replaces the held scan.
    frames = {frameFor(0, 3.0f)};
    hold.augment(frames, 4, dt);
    ASSERT_EQ(frames.size(), 1u);
    frames.clear();
    hold.augment(frames, 5, dt);
    ASSERT_EQ(frames.size(), 1u);
    EXPECT_FLOAT_EQ(frames[0].points[0].distance, 3.0f);
}

// The age bound is the safety half of the contract: a sensor that stops
// delivering (cable pulled mid-show) must stop contributing within the
// window instead of freezing a ghost of the last thing it saw.
TEST(FrameHold, ASilentSensorStopsContributingAfterTheWindow) {
    FrameHold hold(0.25f);
    const float dt = 1.0f / 60.0f;

    std::vector<ScanFrame> frames = {frameFor(0, 2.0f)};
    hold.augment(frames, 0, dt);

    frames.clear();
    hold.augment(frames, 15, dt); // 250 ms at 60 Hz: last tick inside the window
    EXPECT_EQ(frames.size(), 1u);

    frames.clear();
    hold.augment(frames, 16, dt); // past the window: the ghost dies
    EXPECT_TRUE(frames.empty());

    frames.clear();
    hold.augment(frames, 17, dt); // and stays dead
    EXPECT_TRUE(frames.empty());
}

// Sensors hold independently: a fast sensor's fresh frame must not refresh a
// slow sensor's age, and each re-presents its own latest scan.
TEST(FrameHold, SensorsAreHeldIndependently) {
    FrameHold hold(0.25f);
    const float dt = 1.0f / 60.0f;

    std::vector<ScanFrame> frames = {frameFor(0, 2.0f), frameFor(1, 4.0f)};
    hold.augment(frames, 0, dt);
    ASSERT_EQ(frames.size(), 2u);

    // Sensor 0 keeps delivering; sensor 1 goes quiet.
    for (uint64_t tick = 1; tick <= 20; ++tick) {
        frames = {frameFor(0, 2.0f)};
        hold.augment(frames, tick, dt);
        if (tick <= 15) {
            ASSERT_EQ(frames.size(), 2u) << "sensor 1 held at tick " << tick;
        } else {
            ASSERT_EQ(frames.size(), 1u) << "sensor 1 aged out at tick " << tick;
            EXPECT_EQ(frames[0].sensor, 0u);
        }
    }
}

} // namespace
} // namespace sillage
