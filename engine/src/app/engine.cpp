#include "app/engine.h"

#include <chrono>
#include <cstdio>
#include <thread>
#include <utility>

namespace sillage {

namespace {

// Two virtual lidars in opposite corners: multi-sensor from the first line of
// code, so fusion never becomes a retrofit.
std::vector<SensorPose> defaultSensorLayout(Vec2 roomSize) {
    return {
        {{0.15f, 0.15f}, 0.0f},
        {{roomSize.x - 0.15f, roomSize.y - 0.15f}, 3.14159265f},
    };
}

} // namespace

Engine::Engine(EngineConfig config)
    : config_(std::move(config)), simulator_(config_.sim), tracker_(config_.tracker) {
    for (const SensorPose& pose : defaultSensorLayout(config_.sim.roomSize)) {
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
                static_cast<double>(config_.tickHz), static_cast<size_t>(2));

    const auto sensorCount = static_cast<size_t>(2);
    backgrounds_.clear();
    for (size_t i = 0; i < sensorCount; ++i) {
        backgrounds_.emplace_back(config_.sim.raysPerScan, config_.backgroundLearnFrames, 0.15f);
    }

    const float dt = 1.0f / config_.tickHz;
    const auto tickPeriod =
        std::chrono::duration_cast<Duration>(std::chrono::duration<float>(dt));

    running_ = true;
    uint64_t tick = 0;
    auto nextTick = Clock::now();
    while (running_) {
        tickOnce(dt, tick);
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

void Engine::tickOnce(float dt, uint64_t tick) {
    const std::vector<ScanFrame> frames = simulator_.step(dt, Clock::now());

    // Background learning phase: feed the models with an empty-ish room.
    // The simulator has agents from tick 0, so the learned background contains
    // walls plus wherever agents started — good enough for the M0 skeleton;
    // the real learn step (explicit empty-room capture) is part of M1.
    FrameSnapshot snap;
    snap.tick = tick;
    snap.timeSeconds = static_cast<double>(tick) * static_cast<double>(dt);
    snap.roomSize = simulator_.roomSize();

    const auto layout = defaultSensorLayout(config_.sim.roomSize);
    bool anyLearning = false;
    for (const ScanFrame& frame : frames) {
        BackgroundModel& bg = backgrounds_[frame.sensor];
        if (bg.learning()) {
            bg.learn(frame);
            anyLearning = true;
            continue;
        }
        for (const RangePoint& p : frame.points) {
            if (bg.isForeground(p)) {
                snap.foreground.push_back({layout[frame.sensor].toRoom(p), frame.sensor});
            }
        }
    }

    if (!anyLearning) {
        snap.clusters = clusterPoints(snap.foreground, config_.clustering);
        snap.tracks = tracker_.update(snap.clusters, dt, tick);
        osc_.publish(snap);
    }

    if (!config_.headless) {
        // Decimate UI updates to ~30 Hz; points capped by construction (2 lidars).
        if (tick % 2 == 0) {
            server_.broadcast(snapshotToJson(snap));
        }
    }
    snapshot_ = std::move(snap);
}

std::string Engine::snapshotToJson(const FrameSnapshot& snap) const {
    // Hand-rolled JSON: schema lives in protocol/ once the real API server
    // lands; M0 keeps the engine dependency-free.
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
