#pragma once

#include "core/types.h"

#include <optional>
#include <random>
#include <vector>

namespace sillage {

// Synthetic room: rectangular walls, optional circular pillars, walking agents,
// virtual 2D lidars raycasting against all of it. Scenarios (eval/scenarios.h)
// script agents explicitly; the default demo layout lives in the Engine.
class Simulator {
public:
    enum class Motion {
        Line,     // start -> end, then stands still
        PingPong, // start -> end -> start -> ...
        Random,   // random waypoints across the room
    };

    struct AgentSpec {
        Vec2 start{};
        Vec2 end{};
        float speed = 1.2f;      // m/s
        Motion motion = Motion::PingPong;
        float spawnTime = 1.5f;  // seconds; absent (untracked, invisible) before
    };

    struct Pillar {
        Vec2 center{};
        float radius = 0.3f;
    };

    struct Params {
        Vec2 roomSize{10.0f, 8.0f};
        uint32_t raysPerScan = 720;
        float rangeNoiseStd = 0.01f; // meters
        float dropRate = 0.02f;      // fraction of rays lost
        float agentRadius = 0.18f;
        uint32_t seed = 42;
        std::vector<AgentSpec> agents;
        std::vector<Pillar> pillars;
    };

    // Ground-truth position of one live agent.
    struct GroundTruth {
        uint32_t agentIndex = 0;
        Vec2 position{};
    };

    explicit Simulator(const Params& params);

    void addSensor(SensorPose pose) { sensors_.push_back(pose); }
    size_t sensorCount() const { return sensors_.size(); }

    // Advances the world and produces one scan per sensor.
    std::vector<ScanFrame> step(float dt, TimePoint now);

    // Ground truth for spawned agents only (tests, MOT metrics).
    std::vector<GroundTruth> groundTruth() const;
    Vec2 roomSize() const { return params_.roomSize; }

private:
    struct Agent {
        AgentSpec spec;
        Vec2 pos{};
        Vec2 waypoint{};
        bool spawned = false;
        bool done = false; // Line motion finished
    };

    void advanceAgent(Agent& agent, float dt);
    Vec2 randomPointInRoom();
    float raycast(Vec2 origin, Vec2 dir) const;

    Params params_;
    std::vector<SensorPose> sensors_;
    std::vector<Agent> agents_;
    std::mt19937 rng_;
    float time_ = 0.0f;
};

} // namespace sillage
