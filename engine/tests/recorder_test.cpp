#include "pipeline/pipeline.h"
#include "record/recorder.h"
#include "sim/simulator.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <filesystem>

namespace sillage {
namespace {

std::filesystem::path tempFile(const char* name) {
    return std::filesystem::temp_directory_path() / name;
}

TEST(Recorder, RoundtripPreservesFrames) {
    const auto path = tempFile("sillage_roundtrip.srec");
    {
        ScanRecorder rec;
        ASSERT_TRUE(rec.open(path));
        ScanFrame f0;
        f0.sensor = 0;
        f0.points = {{0.1f, 2.0f}, {0.2f, 3.5f}};
        ScanFrame f1;
        f1.sensor = 1;
        f1.points = {{1.5f, 0.75f}};
        rec.write(7, f0);
        rec.write(7, f1);
        rec.write(8, f0);
    }
    ScanReplayer rep;
    ASSERT_TRUE(rep.open(path));

    auto tick7 = rep.nextTick();
    ASSERT_TRUE(tick7.has_value());
    EXPECT_EQ(tick7->first, 7u);
    ASSERT_EQ(tick7->second.size(), 2u);
    EXPECT_EQ(tick7->second[0].sensor, 0u);
    ASSERT_EQ(tick7->second[0].points.size(), 2u);
    EXPECT_FLOAT_EQ(tick7->second[0].points[1].distance, 3.5f);
    EXPECT_EQ(tick7->second[1].sensor, 1u);

    auto tick8 = rep.nextTick();
    ASSERT_TRUE(tick8.has_value());
    EXPECT_EQ(tick8->first, 8u);
    EXPECT_FALSE(rep.nextTick().has_value());
    rep.close(); // Windows locks open files: close before removing
    std::filesystem::remove(path);
}

// The recorder only writes ticks that had frames: a 15 Hz sensor against a
// 60 Hz tick leaves 3-tick gaps in the file. Replay must reproduce that
// timeline — one record group per engine tick would compress it 4x, and the
// replayed tracker would see velocities the live session never had (a 60 s
// field incident "replaying" in 15 s with people moving 4x too fast).
TEST(Recorder, ReplayPreservesRecordedTimelineGaps) {
    const auto path = tempFile("sillage_gaps.srec");
    ScanFrame f;
    f.sensor = 0;
    f.points = {{0.1f, 2.0f}};
    {
        ScanRecorder rec;
        ASSERT_TRUE(rec.open(path));
        // Frames on live ticks 100, 104, 108 — a sparse sensor, and a
        // recording that starts mid-session (rebased so 100 plays at 0).
        rec.write(100, f);
        rec.write(104, f);
        rec.write(108, f);
    }
    ScanReplayer rep;
    ASSERT_TRUE(rep.open(path));

    const auto atTick0 = rep.nextTickAt(0);
    ASSERT_TRUE(atTick0.has_value());
    EXPECT_EQ(atTick0->size(), 1u) << "first record group plays at engine tick 0";
    for (uint64_t t = 1; t <= 3; ++t) {
        const auto gap = rep.nextTickAt(t);
        ASSERT_TRUE(gap.has_value()) << "a gap is not the end of the recording";
        EXPECT_TRUE(gap->empty()) << "tick " << t << " had no frames when live";
    }
    const auto atTick4 = rep.nextTickAt(4);
    ASSERT_TRUE(atTick4.has_value());
    EXPECT_EQ(atTick4->size(), 1u);
    EXPECT_TRUE(rep.nextTickAt(5)->empty());
    EXPECT_TRUE(rep.nextTickAt(6)->empty());
    EXPECT_TRUE(rep.nextTickAt(7)->empty());
    EXPECT_EQ(rep.nextTickAt(8)->size(), 1u);
    EXPECT_FALSE(rep.nextTickAt(9).has_value()) << "recording over";
    rep.close();
    std::filesystem::remove(path);
}

TEST(Recorder, RejectsGarbageFile) {
    const auto path = tempFile("sillage_garbage.srec");
    {
        std::FILE* f = std::fopen(path.string().c_str(), "wb");
        std::fputs("not a recording at all", f);
        std::fclose(f);
    }
    ScanReplayer rep;
    EXPECT_FALSE(rep.open(path));
    std::filesystem::remove(path);
}

// The black-box promise: replaying a recording through a fresh pipeline
// reproduces the exact same tracks (docs/02, docs/09).
TEST(Recorder, ReplayIsDeterministic) {
    const auto path = tempFile("sillage_determinism.srec");
    const std::vector<SensorPose> sensors = {
        {{0.15f, 0.15f}, 0.0f},
        {{9.85f, 7.85f}, 3.14159265f},
    };
    Simulator::Params sp;
    sp.agents = {
        {{1.0f, 1.0f}, {9.0f, 7.0f}, 1.0f, Simulator::Motion::PingPong, 1.5f},
        {{1.0f, 7.0f}, {9.0f, 1.0f}, 1.3f, Simulator::Motion::PingPong, 1.5f},
    };
    Simulator sim(sp);
    for (const SensorPose& pose : sensors) {
        sim.addSensor(pose);
    }

    PipelineConfig cfg;
    cfg.sensors = sensors;
    Pipeline live(cfg);
    const float dt = 1.0f / 60.0f;

    std::vector<Track> liveTracks;
    {
        ScanRecorder rec;
        ASSERT_TRUE(rec.open(path));
        for (uint64_t tick = 0; tick < 600; ++tick) {
            const auto frames = sim.step(dt, TimePoint{});
            for (const ScanFrame& f : frames) {
                rec.write(tick, f);
            }
            liveTracks = live.process(frames, dt, tick, sp.roomSize).tracks;
        }
    }

    Pipeline replayed(cfg);
    ScanReplayer rep;
    ASSERT_TRUE(rep.open(path));
    std::vector<Track> replayTracks;
    while (auto next = rep.nextTick()) {
        replayTracks = replayed.process(next->second, dt, next->first, sp.roomSize).tracks;
    }

    ASSERT_EQ(replayTracks.size(), liveTracks.size());
    for (size_t i = 0; i < liveTracks.size(); ++i) {
        EXPECT_EQ(replayTracks[i].id, liveTracks[i].id);
        EXPECT_FLOAT_EQ(replayTracks[i].position.x, liveTracks[i].position.x);
        EXPECT_FLOAT_EQ(replayTracks[i].position.y, liveTracks[i].position.y);
    }
    rep.close(); // Windows locks open files: close before removing
    std::filesystem::remove(path);
}

} // namespace
} // namespace sillage
