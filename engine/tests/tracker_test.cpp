#include "track/tracker.h"

#include <gtest/gtest.h>

#include <cmath>

namespace sillage {
namespace {

Cluster clusterAt(Vec2 pos) {
    Cluster c;
    c.centroid = pos;
    c.radius = 0.15f;
    c.pointCount = 20;
    return c;
}

TEST(Assignment, PicksGloballyOptimalPairing) {
    // Greedy would assign row0->col0 (cost 1) then row1->col1 (cost 10).
    // Optimal is row0->col1 (2) + row1->col0 (3) = 5.
    const std::vector<float> cost = {1.0f, 2.0f, 3.0f, 10.0f};
    const auto assignment = solveAssignment(cost, 2, 2);
    ASSERT_EQ(assignment.size(), 2u);
    EXPECT_EQ(assignment[0], 1);
    EXPECT_EQ(assignment[1], 0);
}

TEST(Assignment, ForbiddenEntriesStayUnassigned) {
    const std::vector<float> cost = {1e8f};
    const auto assignment = solveAssignment(cost, 1, 1);
    ASSERT_EQ(assignment.size(), 1u);
    EXPECT_EQ(assignment[0], -1);
}

TEST(Tracker, ConfirmsAfterProbationAndKeepsId) {
    Tracker tracker({});
    const float dt = 1.0f / 60.0f;

    std::vector<Track> out;
    for (uint64_t tick = 0; tick < 10; ++tick) {
        const float x = 1.0f + 1.2f * dt * static_cast<float>(tick);
        out = tracker.update({clusterAt({x, 2.0f})}, dt, tick);
    }
    ASSERT_EQ(out.size(), 1u);
    const uint32_t id = out[0].id;
    EXPECT_EQ(out[0].state, TrackState::Confirmed);
    EXPECT_NEAR(out[0].velocity.x, 1.2f, 0.4f);

    for (uint64_t tick = 10; tick < 20; ++tick) {
        const float x = 1.0f + 1.2f * dt * static_cast<float>(tick);
        out = tracker.update({clusterAt({x, 2.0f})}, dt, tick);
    }
    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(out[0].id, id);
}

TEST(Tracker, CoastsThroughShortOcclusionWithSameId) {
    Tracker tracker({});
    const float dt = 1.0f / 60.0f;
    std::vector<Track> out;

    auto posAt = [dt](uint64_t tick) -> Vec2 {
        return {1.0f + 1.0f * dt * static_cast<float>(tick), 3.0f};
    };
    for (uint64_t tick = 0; tick < 30; ++tick) {
        out = tracker.update({clusterAt(posAt(tick))}, dt, tick);
    }
    ASSERT_EQ(out.size(), 1u);
    const uint32_t id = out[0].id;

    // 20 ticks (~330 ms) of total occlusion.
    for (uint64_t tick = 30; tick < 50; ++tick) {
        out = tracker.update({}, dt, tick);
        ASSERT_EQ(out.size(), 1u) << "track must coast, not die";
        EXPECT_EQ(out[0].state, TrackState::Coasting);
    }

    // Reappears where the motion model expects it.
    for (uint64_t tick = 50; tick < 60; ++tick) {
        out = tracker.update({clusterAt(posAt(tick))}, dt, tick);
    }
    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(out[0].id, id);
    EXPECT_EQ(out[0].state, TrackState::Confirmed);
}

TEST(Tracker, CrossingAgentsKeepTheirIds) {
    // Two agents crossing at 90 degrees near (2,2), closest approach ~25 cm.
    // Measurements stay distinct (cluster-merge splitting is M1) — the motion
    // model must carry each identity through the encounter.
    Tracker tracker({});
    const float dt = 1.0f / 60.0f;
    const float speed = 1.2f;

    auto posA = [&](float t) -> Vec2 { return {t * speed, 2.0f}; };          // west -> east
    auto posB = [&](float t) -> Vec2 { return {2.0f, t * speed - 0.35f}; };  // south -> north

    std::vector<Track> out;
    uint32_t idA = 0, idB = 0;
    const auto ticks = static_cast<uint64_t>(3.4f / speed / dt); // full traversal
    for (uint64_t tick = 0; tick < ticks; ++tick) {
        const float t = static_cast<float>(tick) * dt;
        out = tracker.update({clusterAt(posA(t)), clusterAt(posB(t))}, dt, tick);
        if (out.size() == 2 && idA == 0) {
            // Well before the crossing: A is the one walking along y = 2.
            const bool firstIsA = std::abs(out[0].position.y - 2.0f) < 0.2f;
            idA = firstIsA ? out[0].id : out[1].id;
            idB = firstIsA ? out[1].id : out[0].id;
        }
    }

    ASSERT_EQ(out.size(), 2u);
    ASSERT_NE(idA, 0u);
    // Agent A ends east (larger x), agent B ends north on the x = 2 line.
    const Track& endA = out[0].position.x > out[1].position.x ? out[0] : out[1];
    const Track& endB = out[0].id == endA.id ? out[1] : out[0];
    EXPECT_EQ(endA.id, idA) << "ID swap during crossing";
    EXPECT_EQ(endB.id, idB) << "ID swap during crossing";
    EXPECT_NEAR(endA.position.y, 2.0f, 0.3f);
    EXPECT_NEAR(endB.position.x, 2.0f, 0.3f);
}

// Frozen claimants must stay within the shared blob (docs/03 §3 step 4).
// Two confirmed tracks walk into each other and the detector hands the
// tracker ONE unsplit blob: both freeze. Unconfined, their constant-velocity
// predictions keep walking — through each other and out of the blob, where a
// stale prediction later seeds a ghost track (a 9th person in a room of 8,
// i.e. a zone cue firing on nobody). Confinement clamps them to the blob's
// extent for as long as the knot lasts.
TEST(Tracker, FrozenClaimantsStayInsideTheSharedBlob) {
    TrackerParams params;
    Tracker tracker(params);
    const float dt = 1.0f / 60.0f;
    const float speed = 1.0f;
    const Vec2 meet{3.0f, 2.0f};

    // Phase 1: two people converge head-on until 0.5 m apart — separate
    // clusters, both tracks confirmed, velocities pointing at each other.
    uint64_t tick = 0;
    std::vector<Track> out;
    for (float gap = 2.5f; gap > 0.5f; gap -= 2.0f * speed * dt, ++tick) {
        out = tracker.update({clusterAt({meet.x - gap / 2.0f, meet.y}),
                              clusterAt({meet.x + gap / 2.0f, meet.y})},
                             dt, tick);
    }
    ASSERT_EQ(out.size(), 2u) << "both tracks must be confirmed before the knot";

    // Phase 2: one unsplit blob for a full second. Both tracks freeze; their
    // predictions want to keep crossing at ~1 m/s each — unconfined they end
    // ~1 m past the blob on each side.
    Cluster blob;
    blob.centroid = meet;
    blob.radius = 0.35f;
    blob.pointCount = 40;
    const float bound = std::min(blob.radius * params.sharedConfineFactor,
                                 blob.radius + params.sharedCaptureMargin) +
                        0.05f;
    for (int i = 0; i < 60; ++i, ++tick) {
        out = tracker.update({blob}, dt, tick);
        ASSERT_EQ(out.size(), 2u) << "the knot must neither kill a track nor birth a ghost";
        for (const Track& t : out) {
            EXPECT_LE((t.position - meet).norm(), bound)
                << "tick " << i << ": a frozen track escaped the blob";
        }
    }
}

} // namespace
} // namespace sillage
