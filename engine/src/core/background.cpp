#include "core/background.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <numbers>

namespace sillage {

BackgroundModel::BackgroundModel(uint32_t binCount, uint32_t learnFrames, float margin)
    // Clamp to >= 1 bin: binOf() takes a modulo by the bin count, so a
    // zero-bin model (e.g. a hand-edited config) would divide by zero.
    : minRange_(std::max(binCount, 1u), std::numeric_limits<float>::infinity()),
      learnFrames_(learnFrames), margin_(margin) {}

uint32_t BackgroundModel::binOf(float angle) const {
    constexpr float twoPi = 2.0f * std::numbers::pi_v<float>;
    float a = std::fmod(angle, twoPi);
    if (a < 0.0f) {
        a += twoPi;
    }
    const auto bin = static_cast<uint32_t>(a / twoPi * static_cast<float>(minRange_.size()));
    return std::min(bin, static_cast<uint32_t>(minRange_.size() - 1));
}

bool BackgroundModel::learn(const ScanFrame& frame) {
    if (!learning()) {
        return false;
    }
    for (const RangePoint& p : frame.points) {
        if (p.distance <= 0.0f) {
            continue;
        }
        float& bin = minRange_[binOf(p.angle)];
        bin = std::min(bin, p.distance);
    }
    ++framesSeen_;
    return learning();
}

bool BackgroundModel::isForeground(RangePoint p) const {
    if (p.distance <= 0.0f) {
        return false;
    }
    // Compare against the closest background among the bin and its neighbors:
    // rays grazing an occluder's edge jitter between the occluder and whatever
    // lies behind it (mixed-pixel artifact) — the neighbor check keeps those
    // flickering edge points out of the foreground.
    const uint32_t b = binOf(p.angle);
    const auto n = static_cast<uint32_t>(minRange_.size());
    const float bg = std::min({minRange_[b], minRange_[(b + 1) % n], minRange_[(b + n - 1) % n]});
    return p.distance < bg - margin_;
}

} // namespace sillage
