#include "calib/rigid2d.h"

#include <cmath>
#include <random>

namespace sillage {

namespace {

Rigid2D refineOn(const std::vector<Vec2>& from, const std::vector<Vec2>& to,
                 const std::vector<uint32_t>& indices) {
    Vec2 cFrom{}, cTo{};
    for (const uint32_t i : indices) {
        cFrom = cFrom + from[i];
        cTo = cTo + to[i];
    }
    const float inv = 1.0f / static_cast<float>(indices.size());
    cFrom = cFrom * inv;
    cTo = cTo * inv;

    float sCross = 0.0f, sDot = 0.0f;
    for (const uint32_t i : indices) {
        const Vec2 a = from[i] - cFrom;
        const Vec2 b = to[i] - cTo;
        sDot += a.x * b.x + a.y * b.y;
        sCross += a.x * b.y - a.y * b.x;
    }

    Rigid2D result;
    result.theta = std::atan2(sCross, sDot);
    const float c = std::cos(result.theta), s = std::sin(result.theta);
    result.translation = {cTo.x - (c * cFrom.x - s * cFrom.y),
                          cTo.y - (s * cFrom.x + c * cFrom.y)};

    float sumSq = 0.0f;
    for (const uint32_t i : indices) {
        sumSq += (result.apply(from[i]) - to[i]).normSq();
    }
    result.rmse = std::sqrt(sumSq * inv);
    result.inliers = static_cast<uint32_t>(indices.size());
    return result;
}

} // namespace

std::optional<Rigid2D> fitRigid2D(const std::vector<Vec2>& from, const std::vector<Vec2>& to) {
    if (from.size() != to.size() || from.size() < 2) {
        return std::nullopt;
    }
    std::vector<uint32_t> all(from.size());
    for (uint32_t i = 0; i < all.size(); ++i) {
        all[i] = i;
    }
    return refineOn(from, to, all);
}

std::optional<Rigid2D> fitRigid2DRansac(const std::vector<Vec2>& from,
                                        const std::vector<Vec2>& to, float inlierThreshold,
                                        uint32_t iterations, uint32_t seed) {
    const size_t n = from.size();
    if (to.size() != n || n < 2) {
        return std::nullopt;
    }
    std::mt19937 rng(seed);
    std::uniform_int_distribution<uint32_t> pick(0, static_cast<uint32_t>(n - 1));
    const float thresholdSq = inlierThreshold * inlierThreshold;

    std::vector<uint32_t> bestInliers;
    for (uint32_t iter = 0; iter < iterations; ++iter) {
        const uint32_t a = pick(rng);
        const uint32_t b = pick(rng);
        if (a == b || (from[a] - from[b]).normSq() < 0.04f) {
            continue; // degenerate sample (identical or too close: 20 cm min)
        }
        const auto model = refineOn(from, to, {a, b});
        std::vector<uint32_t> inliers;
        for (uint32_t i = 0; i < n; ++i) {
            if ((model.apply(from[i]) - to[i]).normSq() <= thresholdSq) {
                inliers.push_back(i);
            }
        }
        if (inliers.size() > bestInliers.size()) {
            bestInliers = std::move(inliers);
        }
    }
    if (bestInliers.size() < 2) {
        return std::nullopt;
    }
    return refineOn(from, to, bestInliers);
}

} // namespace sillage
