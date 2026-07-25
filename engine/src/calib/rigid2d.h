#pragma once

#include "core/types.h"

#include <cstdint>
#include <optional>
#include <vector>

namespace sillage {

// 2D rigid transform: to = R(theta) * from + t.
struct Rigid2D {
    float theta = 0.0f;
    Vec2 translation{};
    float rmse = 0.0f;   // over inliers
    uint32_t inliers = 0;

    Vec2 apply(Vec2 p) const {
        const float c = std::cos(theta), s = std::sin(theta);
        return {c * p.x - s * p.y + translation.x, s * p.x + c * p.y + translation.y};
    }
};

// Closed-form least-squares rigid fit (Horn/Umeyama, 2D). Needs >= 2 pairs
// with non-degenerate geometry; nullopt otherwise.
std::optional<Rigid2D> fitRigid2D(const std::vector<Vec2>& from, const std::vector<Vec2>& to);

// RANSAC wrapper: tolerates outlier correspondences (occlusions, a second
// person wandering in). inlierThreshold in meters.
std::optional<Rigid2D> fitRigid2DRansac(const std::vector<Vec2>& from,
                                        const std::vector<Vec2>& to,
                                        float inlierThreshold = 0.10f,
                                        uint32_t iterations = 200, uint32_t seed = 1);

} // namespace sillage
