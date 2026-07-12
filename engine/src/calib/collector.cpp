#include "calib/collector.h"

#include <cmath>

namespace sillage {

void CalibrationCollector::addObservation(SensorId sensor, uint64_t tick,
                                          const std::vector<Vec2>& localPoints) {
    if (sensor >= observations_.size() || localPoints.size() < params_.minPointsPerObs) {
        return;
    }

    // Push each surface point one body radius further along its ray from the
    // sensor (local origin): the arc collapses onto the body center, making
    // the observation independent of the viewing direction.
    Vec2 sum{};
    for (const Vec2 p : localPoints) {
        const float d = p.norm();
        if (d < 1e-3f) {
            return;
        }
        sum = sum + p * ((d + params_.personRadius) / d);
    }
    const Vec2 center = sum * (1.0f / static_cast<float>(localPoints.size()));

    // Reject multi-person / smeared ticks: points must hug one body.
    for (const Vec2 p : localPoints) {
        const float d = p.norm();
        const Vec2 pushed = p * ((d + params_.personRadius) / d);
        if ((pushed - center).norm() > params_.maxSpread) {
            return;
        }
    }
    observations_[sensor][tick] = center;
}

std::vector<CalibrationCollector::SensorResult>
CalibrationCollector::solve(SensorId anchorSensor, SensorPose anchorPose) const {
    std::vector<SensorResult> results(observations_.size());
    if (anchorSensor >= observations_.size()) {
        return results;
    }
    results[anchorSensor].pose = anchorPose;
    results[anchorSensor].solved = true;

    const auto& anchorObs = observations_[anchorSensor];
    for (SensorId s = 0; s < observations_.size(); ++s) {
        if (s == anchorSensor) {
            continue;
        }
        // Correspondences: ticks where both sensors saw the walker.
        std::vector<Vec2> from, to; // from = this sensor's frame, to = anchor's
        for (const auto& [tick, center] : observations_[s]) {
            const auto it = anchorObs.find(tick);
            if (it != anchorObs.end()) {
                from.push_back(center);
                to.push_back(it->second);
            }
        }
        SensorResult& r = results[s];
        r.pairs = static_cast<uint32_t>(from.size());
        if (from.size() < params_.minPairs) {
            r.message = "not enough overlap: walk through the shared zone";
            continue;
        }
        const auto rigid = fitRigid2DRansac(from, to, params_.ransacThreshold);
        if (!rigid || rigid->inliers < params_.minPairs / 2) {
            r.message = "no stable transform: too much noise or moving clutter";
            continue;
        }
        // world = anchorPose(p_anchor), p_anchor = M(p_s)
        // => pose_s = { anchorPos + R(anchorTheta) * M.t, anchorTheta + M.theta }
        const float c = std::cos(anchorPose.theta), sn = std::sin(anchorPose.theta);
        r.pose.position = {
            anchorPose.position.x + c * rigid->translation.x - sn * rigid->translation.y,
            anchorPose.position.y + sn * rigid->translation.x + c * rigid->translation.y};
        r.pose.theta = anchorPose.theta + rigid->theta;
        r.rmse = rigid->rmse;
        r.pairs = rigid->inliers;
        r.solved = true;
    }
    return results;
}

} // namespace sillage
