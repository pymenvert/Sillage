#pragma once

#include "core/types.h"

#include <cstdint>
#include <map>
#include <utility>
#include <vector>

namespace sillage {

// Re-presents each sensor's latest scan on ticks where that sensor delivered
// nothing new, bounded by age.
//
// Sensors revolve at their own rate: a TiM at 15 Hz against a 60 Hz tick has
// something new one tick out of four. Without holding, the other three ticks
// simply omit the sensor — with two visible failure modes on site. Fused
// coverage flickers at the sensor's rate (the centroid of a person seen by
// two sensors jumps ~10 cm every time the slow sensor blinks out), and a room
// covered by slow sensors alone tracks NOBODY: detections arrive with 3-tick
// gaps, tentativeMaxMiss kills every probationary track long before its
// confirmHits-th hit.
//
// The age bound keeps this honest: a sensor that stops delivering (cable
// pulled mid-show) stops contributing within maxAgeSeconds instead of
// freezing a ghost of the last thing it saw.
class FrameHold {
public:
    explicit FrameHold(float maxAgeSeconds = 0.25f) : maxAge_(maxAgeSeconds) {}

    // Records this tick's fresh frames, then appends a held copy for every
    // sensor with nothing fresh this tick and a recent-enough previous scan.
    // Call once per tick with a monotonic tick counter.
    void augment(std::vector<ScanFrame>& frames, uint64_t tick, float dt) {
        for (const ScanFrame& f : frames) {
            held_[f.sensor] = {f, tick};
        }
        for (auto it = held_.begin(); it != held_.end();) {
            const float age = static_cast<float>(tick - it->second.second) * dt;
            if (age > maxAge_) {
                it = held_.erase(it); // gone long enough: stop ghosting it
                continue;
            }
            if (age > 0.0f) { // not fresh this tick: re-present the last scan
                frames.push_back(it->second.first);
            }
            ++it;
        }
    }

private:
    float maxAge_;
    // Per sensor: the last fresh scan and the tick it arrived on.
    std::map<SensorId, std::pair<ScanFrame, uint64_t>> held_;
};

} // namespace sillage
