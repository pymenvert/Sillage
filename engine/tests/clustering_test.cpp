#include "detect/clustering.h"

#include <gtest/gtest.h>

namespace sillage {
namespace {

std::vector<WorldPoint> blob(Vec2 center, int count, float spread) {
    std::vector<WorldPoint> pts;
    for (int i = 0; i < count; ++i) {
        const float dx = spread * static_cast<float>(i % 3 - 1) * 0.5f;
        const float dy = spread * static_cast<float>(i / 3 % 3 - 1) * 0.5f;
        pts.push_back({{center.x + dx, center.y + dy}, 0});
    }
    return pts;
}

TEST(Clustering, TwoSeparatedGroupsMakeTwoClusters) {
    auto pts = blob({1.0f, 1.0f}, 9, 0.1f);
    const auto other = blob({4.0f, 4.0f}, 9, 0.1f);
    pts.insert(pts.end(), other.begin(), other.end());

    const auto clusters = clusterPoints(pts, {});
    ASSERT_EQ(clusters.size(), 2u);
}

TEST(Clustering, TwoLegsOfOnePersonMerge) {
    // Two point groups 30 cm apart — legs of one person, must link.
    auto pts = blob({2.0f, 2.0f}, 6, 0.05f);
    const auto leg2 = blob({2.3f, 2.0f}, 6, 0.05f);
    pts.insert(pts.end(), leg2.begin(), leg2.end());

    const auto clusters = clusterPoints(pts, {});
    ASSERT_EQ(clusters.size(), 1u);
    EXPECT_NEAR(clusters[0].centroid.x, 2.15f, 0.05f);
}

TEST(Clustering, NoiseBelowMinPointsIsDropped) {
    std::vector<WorldPoint> pts = {{{0.0f, 0.0f}, 0}, {{5.0f, 5.0f}, 0}};
    const auto clusters = clusterPoints(pts, {});
    EXPECT_TRUE(clusters.empty());
}

TEST(Clustering, EmptyInput) {
    EXPECT_TRUE(clusterPoints({}, {}).empty());
}

} // namespace
} // namespace sillage
