#include "app/engine.h"

#include <chrono>
#include <cstdio>
#include <thread>
#include <utility>

namespace sillage {

namespace {

constexpr Vec2 kDemoRoom{10.0f, 8.0f};

// Two lidars in opposite corners: multi-sensor from the first line of code.
std::vector<SensorPose> demoSensorLayout() {
    return {
        {{0.15f, 0.15f}, 0.0f},
        {{kDemoRoom.x - 0.15f, kDemoRoom.y - 0.15f}, 3.14159265f},
    };
}

Simulator::Params demoSimParams(uint32_t randomAgents, uint32_t seed) {
    Simulator::Params p;
    p.roomSize = kDemoRoom;
    p.seed = seed;
    // Two agents ping-pong the diagonals (repeated crossings at the center),
    // spawning after the background learn phase.
    p.agents = {
        {{1.0f, 1.0f}, {9.0f, 7.0f}, 1.0f, Simulator::Motion::PingPong, 1.5f},
        {{1.0f, 7.0f}, {9.0f, 1.0f}, 1.2f, Simulator::Motion::PingPong, 1.5f},
    };
    for (uint32_t i = 0; i < randomAgents; ++i) {
        p.agents.push_back({{2.0f + static_cast<float>(i), 4.0f},
                            {8.0f - static_cast<float>(i % 3), 5.0f},
                            0.8f,
                            Simulator::Motion::Random,
                            1.5f});
    }
    return p;
}

PipelineConfig demoPipelineConfig() {
    PipelineConfig cfg;
    cfg.sensors = demoSensorLayout();
    return cfg;
}

} // namespace

Engine::Engine(const EngineConfig& config)
    : config_(config), simulator_(demoSimParams(config.randomAgents, config.seed)),
      pipeline_(demoPipelineConfig()) {
    for (const SensorPose& pose : demoSensorLayout()) {
        simulator_.addSensor(pose);
    }
}

bool Engine::run() {
    if (!config_.headless) {
        if (!server_.start(config_.httpBind, config_.httpPort, config_.uiRoot)) {
            std::fprintf(stderr, "error: cannot bind HTTP server on %s:%u\n",
                         config_.httpBind.c_str(), config_.httpPort);
            return false;
        }
        std::printf("UI      : http://%s:%u\n", config_.httpBind.c_str(), config_.httpPort);
    }
    if (!osc_.open(config_.oscHost, config_.oscPort)) {
        std::fprintf(stderr, "error: cannot open OSC destination %s:%u\n",
                     config_.oscHost.c_str(), config_.oscPort);
        return false;
    }
    std::printf("OSC     : %s:%u (Augmenta legacy)\n", config_.oscHost.c_str(), config_.oscPort);
    std::printf("Tick    : %.0f Hz, %zu virtual sensor(s), learning background...\n",
                static_cast<double>(config_.tickHz), simulator_.sensorCount());

    const float dt = 1.0f / config_.tickHz;
    const auto tickPeriod =
        std::chrono::duration_cast<Duration>(std::chrono::duration<float>(dt));

    running_ = true;
    uint64_t tick = 0;
    auto nextTick = Clock::now();
    while (running_) {
        const std::vector<ScanFrame> frames = simulator_.step(dt, Clock::now());
        const FrameSnapshot snap = pipeline_.process(frames, dt, tick, simulator_.roomSize());

        if (!pipeline_.learning()) {
            osc_.publish(snap);
        }
        if (!config_.headless && tick % 2 == 0) { // UI at ~30 Hz
            server_.broadcast(snapshotToJson(snap));
        }

        ++tick;
        if (config_.maxTicks && tick >= *config_.maxTicks) {
            break;
        }
        nextTick += tickPeriod;
        std::this_thread::sleep_until(nextTick);
    }
    server_.stop();
    return true;
}

std::string Engine::snapshotToJson(const FrameSnapshot& snap) const {
    // Hand-rolled JSON: schema lives in protocol/ once the real API server
    // lands; M0/M1 keep the engine dependency-free.
    std::string out;
    out.reserve(snap.foreground.size() * 24 + snap.tracks.size() * 96 + 128);
    char buf[128];

    out += "{\"tick\":";
    out += std::to_string(snap.tick);
    std::snprintf(buf, sizeof(buf), ",\"room\":[%.2f,%.2f]", static_cast<double>(snap.roomSize.x),
                  static_cast<double>(snap.roomSize.y));
    out += buf;

    out += ",\"points\":[";
    for (size_t i = 0; i < snap.foreground.size(); ++i) {
        const WorldPoint& p = snap.foreground[i];
        std::snprintf(buf, sizeof(buf), "%s[%.3f,%.3f,%u]", i ? "," : "",
                      static_cast<double>(p.pos.x), static_cast<double>(p.pos.y), p.sensor);
        out += buf;
    }

    out += "],\"tracks\":[";
    for (size_t i = 0; i < snap.tracks.size(); ++i) {
        const Track& t = snap.tracks[i];
        std::snprintf(buf, sizeof(buf),
                      "%s{\"id\":%u,\"oid\":%u,\"x\":%.3f,\"y\":%.3f,\"vx\":%.3f,\"vy\":%.3f,"
                      "\"r\":%.3f,\"coast\":%s}",
                      i ? "," : "", t.id, t.oid, static_cast<double>(t.position.x),
                      static_cast<double>(t.position.y), static_cast<double>(t.velocity.x),
                      static_cast<double>(t.velocity.y), static_cast<double>(t.radius),
                      t.state == TrackState::Coasting ? "true" : "false");
        out += buf;
    }
    out += "]}";
    return out;
}

} // namespace sillage
