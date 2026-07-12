#include "eval/metrics.h"

#include <gtest/gtest.h>

namespace sillage {
namespace {

Track trackAt(uint32_t id, Vec2 pos) {
    Track t;
    t.id = id;
    t.position = pos;
    return t;
}

TEST(MotMetrics, PerfectTrackingScoresPerfect) {
    MotAccumulator mot;
    for (int f = 0; f < 100; ++f) {
        const float x = 0.05f * static_cast<float>(f);
        mot.addFrame({{0, {x, 1.0f}}, {1, {x, 3.0f}}},
                     {trackAt(10, {x, 1.0f}), trackAt(11, {x, 3.0f})});
    }
    const MotResult r = mot.result();
    EXPECT_EQ(r.idSwitches, 0);
    EXPECT_EQ(r.misses, 0);
    EXPECT_EQ(r.falsePositives, 0);
    EXPECT_FLOAT_EQ(r.mota, 1.0f);
    EXPECT_FLOAT_EQ(r.idf1, 1.0f);
}

TEST(MotMetrics, IdSwapIsCountedAndDegradesIdf1) {
    MotAccumulator mot;
    // Agent 0 tracked by id 10 for 50 frames, then by id 11 (one switch).
    for (int f = 0; f < 50; ++f) {
        mot.addFrame({{0, {1.0f, 1.0f}}}, {trackAt(10, {1.0f, 1.0f})});
    }
    for (int f = 0; f < 50; ++f) {
        mot.addFrame({{0, {1.0f, 1.0f}}}, {trackAt(11, {1.0f, 1.0f})});
    }
    const MotResult r = mot.result();
    EXPECT_EQ(r.idSwitches, 1);
    // Best identity mapping covers half the frames: IDF1 = 2*50/(100+100).
    EXPECT_NEAR(r.idf1, 0.5f, 1e-4f);
}

TEST(MotMetrics, MissAndFalsePositiveAccounting) {
    MotAccumulator mot;
    // One agent, no track: miss. Plus one track far away: false positive.
    mot.addFrame({{0, {1.0f, 1.0f}}}, {trackAt(10, {5.0f, 5.0f})});
    const MotResult r = mot.result();
    EXPECT_EQ(r.misses, 1);
    EXPECT_EQ(r.falsePositives, 1);
}

} // namespace
} // namespace sillage
