#include "sim/simulator.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

namespace sillage {

Simulator::Simulator(const Params& params) : params_(params), rng_(params.seed) {
    // Crossing agents walk diagonals through the room center (X pattern),
    // guaranteeing repeated close encounters — the tracker's core test case.
    const Vec2 r = params_.roomSize;
    const float m = 0.8f; // wall margin
    const std::vector<std::pair<Vec2, Vec2>> diagonals = {
        {{m, m}, {r.x - m, r.y - m}},
        {{m, r.y - m}, {r.x - m, m}},
    };
    for (uint32_t i = 0; i < params_.crossingAgents; ++i) {
        Agent a;
        const auto& d = diagonals[i % diagonals.size()];
        a.pos = (i % 2 == 0) ? d.first : d.second;
        a.waypoint = (i % 2 == 0) ? d.second : d.first;
        a.crossing = true;
        a.speed = 1.0f + 0.2f * static_cast<float>(i);
        agents_.push_back(a);
    }
    for (uint32_t i = 0; i < params_.randomAgents; ++i) {
        Agent a;
        a.pos = randomPointInRoom();
        a.waypoint = randomPointInRoom();
        a.speed = 0.8f;
        agents_.push_back(a);
    }
}

Vec2 Simulator::randomPointInRoom() {
    std::uniform_real_distribution<float> ux(0.5f, params_.roomSize.x - 0.5f);
    std::uniform_real_distribution<float> uy(0.5f, params_.roomSize.y - 0.5f);
    return {ux(rng_), uy(rng_)};
}

void Simulator::advanceAgent(Agent& agent, float dt) {
    const Vec2 toWp = agent.waypoint - agent.pos;
    const float dist = toWp.norm();
    if (dist < 0.05f) {
        if (agent.crossing) {
            // Mirror through the room center: walk the diagonal back, passing
            // through the middle again.
            agent.waypoint = {params_.roomSize.x - agent.pos.x, params_.roomSize.y - agent.pos.y};
        } else {
            agent.waypoint = randomPointInRoom();
        }
        return;
    }
    const Vec2 dir = toWp * (1.0f / dist);
    const float step = std::min(dist, agent.speed * dt);
    agent.pos = agent.pos + dir * step;
}

float Simulator::raycast(Vec2 origin, Vec2 dir) const {
    float best = std::numeric_limits<float>::infinity();

    // Room walls: x=0, x=W, y=0, y=H.
    if (std::abs(dir.x) > 1e-6f) {
        for (const float wx : {0.0f, params_.roomSize.x}) {
            const float t = (wx - origin.x) / dir.x;
            if (t > 0.0f) {
                const float y = origin.y + t * dir.y;
                if (y >= 0.0f && y <= params_.roomSize.y) {
                    best = std::min(best, t);
                }
            }
        }
    }
    if (std::abs(dir.y) > 1e-6f) {
        for (const float wy : {0.0f, params_.roomSize.y}) {
            const float t = (wy - origin.y) / dir.y;
            if (t > 0.0f) {
                const float x = origin.x + t * dir.x;
                if (x >= 0.0f && x <= params_.roomSize.x) {
                    best = std::min(best, t);
                }
            }
        }
    }

    // Agents as circles.
    for (const Agent& a : agents_) {
        const Vec2 oc = a.pos - origin;
        const float proj = oc.dot(dir);
        if (proj <= 0.0f) {
            continue;
        }
        const float perpSq = oc.normSq() - proj * proj;
        const float r2 = params_.agentRadius * params_.agentRadius;
        if (perpSq > r2) {
            continue;
        }
        const float t = proj - std::sqrt(r2 - perpSq);
        if (t > 0.0f) {
            best = std::min(best, t);
        }
    }
    return best;
}

std::vector<ScanFrame> Simulator::step(float dt, TimePoint now) {
    for (Agent& a : agents_) {
        advanceAgent(a, dt);
    }

    std::uniform_real_distribution<float> uniform(0.0f, 1.0f);
    std::normal_distribution<float> noise(0.0f, params_.rangeNoiseStd);

    std::vector<ScanFrame> frames;
    frames.reserve(sensors_.size());
    for (size_t s = 0; s < sensors_.size(); ++s) {
        ScanFrame frame;
        frame.sensor = static_cast<SensorId>(s);
        frame.captureTime = now;
        frame.points.reserve(params_.raysPerScan);
        for (uint32_t i = 0; i < params_.raysPerScan; ++i) {
            const float localAngle =
                2.0f * std::numbers::pi_v<float> * static_cast<float>(i) /
                static_cast<float>(params_.raysPerScan);
            if (uniform(rng_) < params_.dropRate) {
                continue;
            }
            const float worldAngle = sensors_[s].theta + localAngle;
            const Vec2 dir{std::cos(worldAngle), std::sin(worldAngle)};
            const float hit = raycast(sensors_[s].position, dir);
            if (!std::isfinite(hit)) {
                continue;
            }
            frame.points.push_back({localAngle, hit + noise(rng_)});
        }
        frames.push_back(std::move(frame));
    }
    return frames;
}

std::vector<Vec2> Simulator::agentPositions() const {
    std::vector<Vec2> out;
    out.reserve(agents_.size());
    for (const Agent& a : agents_) {
        out.push_back(a.pos);
    }
    return out;
}

} // namespace sillage
