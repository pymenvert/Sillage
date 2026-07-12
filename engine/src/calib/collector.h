#pragma once

#include "calib/rigid2d.h"
#include "core/types.h"

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace sillage {

// Walk-based auto-calibration (docs/04 §3).
//
// Procedure: one person walks through the sensor overlaps while each sensor
// observes alone (no fusion needed — poses are precisely what we do not know
// yet). Each sensor sees the walker as an arc of surface points; the arc's
// pushed centroid estimates the body center, a viewpoint-independent physical
// point. Matching the per-tick centers between sensor pairs constrains their
// relative pose; the anchor sensor pins the room frame.
class CalibrationCollector {
public:
    struct Params {
        float personRadius = 0.16f;   // pushed-center correction (docs/04)
        uint32_t minPointsPerObs = 4; // fewer = unreliable center
        float maxSpread = 0.45f;      // larger = probably 2 people, skip tick
        uint32_t minPairs = 60;       // correspondences needed per sensor pair
        float ransacThreshold = 0.10f;
    };

    explicit CalibrationCollector(size_t sensorCount, Params params = {})
        : params_(params), observations_(sensorCount) {}

    // Feed the walker's foreground points seen by one sensor at one tick,
    // in the SENSOR-LOCAL cartesian frame. Ignores ambiguous ticks.
    void addObservation(SensorId sensor, uint64_t tick, const std::vector<Vec2>& localPoints);

    size_t observationCount(SensorId sensor) const { return observations_[sensor].size(); }

    struct SensorResult {
        SensorPose pose{};
        float rmse = 0.0f;      // pair residual, meters
        uint32_t pairs = 0;     // correspondences used
        bool solved = false;
        std::string message;
    };

    // Solves every sensor's pose relative to the anchor. anchorPose pins the
    // room frame (the anchor's result echoes it with solved=true).
    std::vector<SensorResult> solve(SensorId anchorSensor, SensorPose anchorPose) const;

private:
    Params params_;
    // Per sensor: tick -> estimated body center in sensor-local frame.
    std::vector<std::map<uint64_t, Vec2>> observations_;
};

} // namespace sillage
