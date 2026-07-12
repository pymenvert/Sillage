#pragma once

#include "core/types.h"

#include <random>
#include <vector>

namespace sillage {

// Synthetic room: rectangular walls, walking agents, virtual 2D lidars doing
// raycasts against walls and agents (one circle per person at M0 — the
// two-legs model arrives with the M1 scenario library).
class Simulator {
public:
    struct Params {
        Vec2 roomSize{10.0f, 8.0f};
        uint32_t raysPerScan = 720;
        float rangeNoiseStd = 0.01f; // meters
        float dropRate = 0.02f;      // fraction of rays lost
        float agentRadius = 0.18f;
        uint32_t crossingAgents = 2; // walk an X pattern through the center
        uint32_t randomAgents = 1;   // random waypoint walkers
        uint32_t seed = 42;
    };

    explicit Simulator(const Params& params);

    void addSensor(SensorPose pose) { sensors_.push_back(pose); }

    // Advances the world and produces one scan per sensor.
    std::vector<ScanFrame> step(float dt, TimePoint now);

    // Ground truth for tests and (later) MOT metrics.
    std::vector<Vec2> agentPositions() const;
    Vec2 roomSize() const { return params_.roomSize; }

private:
    struct Agent {
        Vec2 pos{};
        Vec2 waypoint{};
        float speed = 1.2f; // m/s
        bool crossing = false;
    };

    void advanceAgent(Agent& agent, float dt);
    Vec2 randomPointInRoom();
    float raycast(Vec2 origin, Vec2 dir) const;

    Params params_;
    std::vector<SensorPose> sensors_;
    std::vector<Agent> agents_;
    std::mt19937 rng_;
};

} // namespace sillage
