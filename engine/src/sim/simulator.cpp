#include "sim/simulator.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numbers>

namespace sillage {

Simulator::Simulator(const Params& params) : params_(params), rng_(params.seed) {
    for (const AgentSpec& spec : params_.agents) {
        Agent a;
        a.spec = spec;
        a.pos = spec.start;
        a.waypoint = spec.end;
        agents_.push_back(a);
    }
}

Vec2 Simulator::randomPointInRoom() {
    std::uniform_real_distribution<float> ux(0.5f, params_.roomSize.x - 0.5f);
    std::uniform_real_distribution<float> uy(0.5f, params_.roomSize.y - 0.5f);
    return {ux(rng_), uy(rng_)};
}

void Simulator::advanceAgent(Agent& agent, float dt) {
    if (!agent.spawned || agent.done) {
        return;
    }
    const Vec2 toWp = agent.waypoint - agent.pos;
    const float dist = toWp.norm();
    if (dist < 0.05f) {
        switch (agent.spec.motion) {
        case Motion::Line:
            agent.done = true;
            return;
        case Motion::PingPong:
            agent.waypoint =
                (agent.waypoint - agent.spec.end).normSq() < 1e-4f ? agent.spec.start
                                                                   : agent.spec.end;
            return;
        case Motion::Random:
            agent.waypoint = randomPointInRoom();
            return;
        }
        return;
    }
    const Vec2 dir = toWp * (1.0f / dist);
    const float step = std::min(dist, agent.spec.speed * dt);
    agent.pos = agent.pos + dir * step;

    // Personal-space repulsion: humans do not walk through each other. Without
    // this, two agents can occupy the same spot for seconds — a physically
    // impossible lump no tracker (and no competitor) could disambiguate.
    constexpr float kPersonalSpace = 0.45f;
    constexpr float kRepulsionGain = 2.0f; // m/s per meter of intrusion
    for (const Agent& other : agents_) {
        if (&other == &agent || !other.spawned) {
            continue;
        }
        const Vec2 away = agent.pos - other.pos;
        const float d = away.norm();
        if (d > 1e-4f && d < kPersonalSpace) {
            agent.pos = agent.pos + away * (kRepulsionGain * (kPersonalSpace - d) * dt / d);
        }
    }
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

    // Circle intersection helper for agents and pillars.
    const auto hitCircle = [&](Vec2 center, float radius) {
        const Vec2 oc = center - origin;
        const float proj = oc.dot(dir);
        if (proj <= 0.0f) {
            return;
        }
        const float perpSq = oc.normSq() - proj * proj;
        const float r2 = radius * radius;
        if (perpSq > r2) {
            return;
        }
        const float t = proj - std::sqrt(r2 - perpSq);
        if (t > 0.0f) {
            best = std::min(best, t);
        }
    };

    for (const Agent& a : agents_) {
        if (a.spawned) {
            hitCircle(a.pos, params_.agentRadius);
        }
    }
    for (const Pillar& p : params_.pillars) {
        hitCircle(p.center, p.radius);
    }
    return best;
}

std::vector<ScanFrame> Simulator::step(float dt, TimePoint now) {
    time_ += dt;
    for (Agent& a : agents_) {
        if (!a.spawned && time_ >= a.spec.spawnTime) {
            a.spawned = true;
        }
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

std::vector<Simulator::GroundTruth> Simulator::groundTruth() const {
    std::vector<GroundTruth> out;
    out.reserve(agents_.size());
    for (uint32_t i = 0; i < agents_.size(); ++i) {
        if (agents_[i].spawned) {
            out.push_back({i, agents_[i].pos});
        }
    }
    return out;
}

} // namespace sillage
