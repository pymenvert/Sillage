#include "calib/collector.h"
#include "calib/rigid2d.h"
#include "core/background.h"
#include "pipeline/pipeline.h"
#include "sim/simulator.h"

#include <gtest/gtest.h>

#include <cmath>
#include <numbers>
#include <random>

namespace sillage {
namespace {

constexpr float kPi = std::numbers::pi_v<float>;

TEST(Rigid2D, RecoversExactTransform) {
    const Rigid2D truth{0.7f, {2.0f, -1.0f}, 0.0f, 0};
    std::vector<Vec2> from = {{0, 0}, {1, 0}, {0, 1}, {3, 2}, {-1, 4}};
    std::vector<Vec2> to;
    for (const Vec2 p : from) {
        to.push_back(truth.apply(p));
    }
    const auto fit = fitRigid2D(from, to);
    ASSERT_TRUE(fit.has_value());
    EXPECT_NEAR(fit->theta, truth.theta, 1e-5f);
    EXPECT_NEAR(fit->translation.x, truth.translation.x, 1e-5f);
    EXPECT_NEAR(fit->translation.y, truth.translation.y, 1e-5f);
    EXPECT_LT(fit->rmse, 1e-5f);
}

TEST(Rigid2D, RansacSurvivesNoiseAndOutliers) {
    const Rigid2D truth{-2.4f, {-3.0f, 5.5f}, 0.0f, 0};
    std::mt19937 rng(7);
    std::normal_distribution<float> noise(0.0f, 0.01f);
    std::uniform_real_distribution<float> coord(0.0f, 8.0f);

    std::vector<Vec2> from, to;
    for (int i = 0; i < 200; ++i) {
        const Vec2 p{coord(rng), coord(rng)};
        from.push_back(p);
        Vec2 mapped = truth.apply(p);
        mapped.x += noise(rng);
        mapped.y += noise(rng);
        if (i % 10 == 0) { // 10% garbage correspondences
            mapped.x += 2.0f;
        }
        to.push_back(mapped);
    }
    const auto fit = fitRigid2DRansac(from, to);
    ASSERT_TRUE(fit.has_value());
    EXPECT_NEAR(fit->theta, truth.theta, 0.01f);
    EXPECT_NEAR(fit->translation.x, truth.translation.x, 0.03f);
    EXPECT_NEAR(fit->translation.y, truth.translation.y, 0.03f);
    EXPECT_GT(fit->inliers, 150u);
}

TEST(Rigid2D, RejectsDegenerateInput) {
    EXPECT_FALSE(fitRigid2D({{1, 1}}, {{2, 2}}).has_value());
    EXPECT_FALSE(fitRigid2DRansac({}, {}).has_value());
}

// End-to-end: two simulated sensors with known true poses, one calibration
// walker. Feed raw per-sensor foreground to the collector, recover sensor 1's
// pose from sensor 0 — the docs/04 acceptance is < 5 cm.
TEST(Calibration, RecoversSensorPoseFromWalk) {
    const SensorPose truth0{{0.15f, 0.15f}, 0.0f};
    const SensorPose truth1{{9.85f, 7.85f}, kPi};

    Simulator::Params sp;
    sp.roomSize = {10.0f, 8.0f};
    sp.agents = {{{1.0f, 1.0f}, {9.0f, 7.0f}, 1.1f, Simulator::Motion::PingPong, 1.5f}};
    Simulator sim(sp);
    sim.addSensor(truth0);
    sim.addSensor(truth1);

    std::vector<BackgroundModel> backgrounds;
    backgrounds.emplace_back(720u, 60u, 0.15f);
    backgrounds.emplace_back(720u, 60u, 0.15f);

    CalibrationCollector collector(2);
    const float dt = 1.0f / 60.0f;
    const auto ticks = static_cast<uint64_t>(30.0f / dt);
    for (uint64_t tick = 0; tick < ticks; ++tick) {
        for (const ScanFrame& frame : sim.step(dt, TimePoint{})) {
            BackgroundModel& bg = backgrounds[frame.sensor];
            if (bg.learning()) {
                bg.learn(frame);
                continue;
            }
            std::vector<Vec2> local;
            for (const RangePoint& p : frame.points) {
                if (bg.isForeground(p)) {
                    local.push_back({p.distance * std::cos(p.angle),
                                     p.distance * std::sin(p.angle)});
                }
            }
            collector.addObservation(frame.sensor, tick, local);
        }
    }

    ASSERT_GT(collector.observationCount(0), 500u);
    ASSERT_GT(collector.observationCount(1), 500u);

    const auto results = collector.solve(0, truth0);
    ASSERT_TRUE(results[1].solved) << results[1].message;

    const float posError = (results[1].pose.position - truth1.position).norm();
    float angleError = results[1].pose.theta - truth1.theta;
    while (angleError > kPi) { angleError -= 2.0f * kPi; }
    while (angleError < -kPi) { angleError += 2.0f * kPi; }

    EXPECT_LT(posError, 0.05f) << "recovered (" << results[1].pose.position.x << ","
                               << results[1].pose.position.y << ") rmse=" << results[1].rmse;
    EXPECT_LT(std::abs(angleError), 0.025f); // ~1.4 degrees
    EXPECT_LT(results[1].rmse, 0.06f);
}

// The full live-engine chain a calibration workflow will use: a Pipeline
// running with WRONG poses, the collector fed from the pipeline's fused
// room-frame foreground (mapped back to sensor-local via SensorPose::toLocal
// — the exact inverse of the fusion transform), and the solver recovering the
// true pose. This is the CI stand-in for "walk the room during setup".
TEST(Calibration, RecoversPoseFromLivePipelineWithWrongPoses) {
    const SensorPose truth0{{0.15f, 0.15f}, 0.0f};
    const SensorPose truth1{{9.85f, 7.85f}, kPi};
    // What the operator typed in from a tape measure: half a meter and ~11
    // degrees off on sensor 1 — enough to split one person into two clusters.
    const SensorPose wrong1{{9.45f, 7.55f}, kPi + 0.2f};

    Simulator::Params sp;
    sp.roomSize = {10.0f, 8.0f};
    sp.agents = {{{1.0f, 1.0f}, {9.0f, 7.0f}, 1.1f, Simulator::Motion::PingPong, 1.5f}};
    Simulator sim(sp);
    sim.addSensor(truth0); // the simulator scans from the TRUE poses
    sim.addSensor(truth1);

    PipelineConfig cfg;
    cfg.sensors = {truth0, wrong1}; // the pipeline believes the WRONG pose
    cfg.backgroundBins = 720;
    cfg.backgroundLearnFrames = 60;
    Pipeline pipeline(cfg);

    CalibrationCollector collector(2);
    const float dt = 1.0f / 60.0f;
    const auto ticks = static_cast<uint64_t>(30.0f / dt);
    for (uint64_t tick = 0; tick < ticks; ++tick) {
        const auto snap = pipeline.process(sim.step(dt, TimePoint{}), dt, tick, sp.roomSize);
        // Feed per-sensor local observations from the fused foreground — the
        // believed pose cancels exactly (toLocal inverts toRoom), so the
        // collector sees true sensor-local data even under a wrong pose.
        std::vector<std::vector<Vec2>> local(2);
        for (const WorldPoint& p : snap.foreground) {
            local[p.sensor].push_back(cfg.sensors[p.sensor].toLocal(p.pos));
        }
        for (SensorId s = 0; s < 2; ++s) {
            collector.addObservation(s, tick, local[s]);
        }
    }

    const auto results = collector.solve(0, truth0);
    ASSERT_TRUE(results[1].solved) << results[1].message;
    const float posError = (results[1].pose.position - truth1.position).norm();
    float angleError = results[1].pose.theta - truth1.theta;
    while (angleError > kPi) { angleError -= 2.0f * kPi; }
    while (angleError < -kPi) { angleError += 2.0f * kPi; }
    EXPECT_LT(posError, 0.05f) << "recovered (" << results[1].pose.position.x << ","
                               << results[1].pose.position.y << ") rmse=" << results[1].rmse;
    EXPECT_LT(std::abs(angleError), 0.025f);
}

// A sensor with no overlap must fail loudly, not return garbage.
TEST(Calibration, ReportsInsufficientOverlap) {
    CalibrationCollector collector(2);
    for (uint64_t t = 0; t < 100; ++t) {
        collector.addObservation(0, t, {{1, 1}, {1.1f, 1}, {1, 1.1f}, {1.1f, 1.1f}});
        // Sensor 1 never sees the walker at the same ticks.
        collector.addObservation(1, t + 1000, {{2, 2}, {2.1f, 2}, {2, 2.1f}, {2.1f, 2.1f}});
    }
    const auto results = collector.solve(0, SensorPose{});
    EXPECT_FALSE(results[1].solved);
    EXPECT_FALSE(results[1].message.empty());
}

} // namespace
} // namespace sillage
