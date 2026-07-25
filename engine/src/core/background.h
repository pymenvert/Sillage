#pragma once

#include "core/types.h"

#include <cstdint>
#include <vector>

namespace sillage {

// Per-sensor static background model over angular bins.
//
// Learns the minimum stable range per bin during a learning phase, then
// classifies points as foreground when they land clearly in front of the
// learned background. This is the same principle the real pipeline will use;
// M0 keeps it fixed after learning (adaptive mode comes with M1).
class BackgroundModel {
public:
    BackgroundModel(uint32_t binCount, uint32_t learnFrames, float margin);

    // Feeds a frame. Returns true while still learning.
    bool learn(const ScanFrame& frame);
    bool learning() const { return framesSeen_ < learnFrames_; }

    // True if this point is in front of the background by more than the margin.
    bool isForeground(RangePoint p) const;

private:
    uint32_t binOf(float angle) const;

    std::vector<float> minRange_; // per bin; +inf until observed
    uint32_t learnFrames_;
    uint32_t framesSeen_ = 0;
    float margin_;
};

} // namespace sillage
