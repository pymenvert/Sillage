#include "app/engine.h"

#include <chrono>
#include <cstdio>
#include <thread>
#include <utility>

namespace sillage {

namespace {

// Two virtual lidars in opposite corners: multi-sensor from the first line of
// code, so fusion never becomes a retrofit.
std::vector<SensorPose> demoSensorLayout(Vec2 room) {
    return {
        {{0.15f, 0.15f}, 0.0f},
        {{room.x - 0.15f, room.y - 0.15f}, 3.14159265f},
    };
}

Simulator::Params demoSimParams(Vec2 room, uint32_t randomAgents, uint32_t seed) {
    Simulator::Params p;
    p.roomSize = room;
    p.seed = seed;
    // Two agents ping-pong the diagonals (repeated crossings at the center),
    // spawning after the background learn phase.
    p.agents = {
        {{1.0f, 1.0f}, {room.x - 1.0f, room.y - 1.0f}, 1.0f, Simulator::Motion::PingPong, 1.5f},
        {{1.0f, room.y - 1.0f}, {room.x - 1.0f, 1.0f}, 1.2f, Simulator::Motion::PingPong, 1.5f},
    };
    for (uint32_t i = 0; i < randomAgents; ++i) {
        p.agents.push_back({{2.0f + static_cast<float>(i % 6), room.y * 0.5f},
                            {room.x - 2.0f, room.y * 0.6f},
                            0.8f,
                            Simulator::Motion::Random,
                            1.5f});
    }
    return p;
}

} // namespace

std::vector<SensorPose> Engine::sensorLayout() const {
    std::vector<SensorPose> layout;
    if (config_.simEnabled) {
        layout = demoSensorLayout(config_.roomSize);
    }
    for (const HokuyoSensorConfig& h : config_.hokuyos) {
        layout.push_back(h.pose);
    }
    return layout;
}

Engine::Engine(const EngineConfig& config)
    : config_(config), pipeline_([&] {
          PipelineConfig cfg;
          cfg.sensors = sensorLayout();
          cfg.backgroundBins = 1440; // covers both sim (720 rays) and UST (1081 steps)
          return cfg;
      }()) {
    if (config_.simEnabled) {
        simulator_ = std::make_unique<Simulator>(
            demoSimParams(config_.roomSize, config_.randomAgents, config_.seed));
        for (const SensorPose& pose : demoSensorLayout(config_.roomSize)) {
            simulator_->addSensor(pose);
        }
    }
    const auto simSensors = static_cast<SensorId>(config_.simEnabled ? 2 : 0);
    for (size_t i = 0; i < config_.hokuyos.size(); ++i) {
        HokuyoDriver::Config hc;
        hc.host = config_.hokuyos[i].host;
        hc.port = config_.hokuyos[i].port;
        hc.sensorId = simSensors + static_cast<SensorId>(i);
        hokuyos_.push_back(std::make_unique<HokuyoDriver>(std::move(hc)));
        hokuyoSeqs_.push_back(0);
    }
}

bool Engine::run() {
    if (!config_.headless) {
        server_.setApiHandler([this](const std::string& path) -> std::string {
            if (path == "/api/status") {
                return statusJson();
            }
            return {};
        });
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
    for (auto& driver : hokuyos_) {
        driver->start();
    }
    std::printf("OSC     : %s:%u (Augmenta legacy)\n", config_.oscHost.c_str(), config_.oscPort);
    std::printf("Sensors : %zu virtual, %zu hokuyo\n",
                static_cast<size_t>(config_.simEnabled ? 2 : 0), hokuyos_.size());
    std::printf("Tick    : %.0f Hz, learning background...\n",
                static_cast<double>(config_.tickHz));

    const float dt = 1.0f / config_.tickHz;
    const auto tickPeriod =
        std::chrono::duration_cast<Duration>(std::chrono::duration<float>(dt));

    running_ = true;
    uint64_t tick = 0;
    auto nextTick = Clock::now();
    while (running_) {
        const auto tickStart = Clock::now();

        std::vector<ScanFrame> frames;
        if (simulator_) {
            frames = simulator_->step(dt, tickStart);
        }
        for (size_t i = 0; i < hokuyos_.size(); ++i) {
            if (auto frame = hokuyos_[i]->latestFrame(hokuyoSeqs_[i])) {
                frames.push_back(std::move(*frame));
            }
        }

        const FrameSnapshot snap = pipeline_.process(frames, dt, tick, config_.roomSize);
        if (!pipeline_.learning()) {
            osc_.publish(snap);
        }

        const float tickMs =
            std::chrono::duration<float, std::milli>(Clock::now() - tickStart).count();
        {
            std::lock_guard lock(statsMutex_);
            tickMsAvg_ = 0.98f * tickMsAvg_ + 0.02f * tickMs;
            tickMsMax_ = std::max(tickMsMax_ * 0.999f, tickMs); // decaying peak
            tracksNow_ = static_cast<uint32_t>(snap.tracks.size());
            learning_ = pipeline_.learning();
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
    for (auto& driver : hokuyos_) {
        driver->stop();
    }
    server_.stop();
    return true;
}

std::string Engine::statusJson() const {
    std::string out;
    char buf[256];
    float avg, mx;
    uint32_t tracks;
    bool learning;
    {
        std::lock_guard lock(statsMutex_);
        avg = tickMsAvg_;
        mx = tickMsMax_;
        tracks = tracksNow_;
        learning = learning_;
    }
    std::snprintf(buf, sizeof(buf),
                  "{\"version\":\"%s\",\"tickMsAvg\":%.3f,\"tickMsMax\":%.3f,\"tracks\":%u,"
                  "\"learning\":%s,\"clients\":%zu,\"sensors\":[",
                  "0.1.0", static_cast<double>(avg), static_cast<double>(mx), tracks,
                  learning ? "true" : "false", server_.clientCount());
    out += buf;
    bool first = true;
    if (config_.simEnabled) {
        for (int i = 0; i < 2; ++i) {
            std::snprintf(buf, sizeof(buf),
                          "%s{\"id\":%d,\"type\":\"sim\",\"connected\":true,\"fps\":%.0f}",
                          first ? "" : ",", i, static_cast<double>(config_.tickHz));
            out += buf;
            first = false;
        }
    }
    const auto simSensors = config_.simEnabled ? 2 : 0;
    for (size_t i = 0; i < hokuyos_.size(); ++i) {
        const SensorHealth h = hokuyos_[i]->health();
        std::snprintf(buf, sizeof(buf),
                      "%s{\"id\":%zu,\"type\":\"hokuyo\",\"connected\":%s,\"fps\":%.1f,"
                      "\"frames\":%llu,\"errors\":%llu}",
                      first ? "" : ",", simSensors + i, h.connected ? "true" : "false",
                      static_cast<double>(h.scansPerSecond),
                      static_cast<unsigned long long>(h.framesReceived),
                      static_cast<unsigned long long>(h.decodeErrors));
        out += buf;
        first = false;
    }
    out += "]}";
    return out;
}

std::string Engine::snapshotToJson(const FrameSnapshot& snap) const {
    // Hand-rolled JSON: schema lives in protocol/ once the real API server
    // lands; M0/M1 keep the engine dependency-free.
    std::string out;
    out.reserve(snap.foreground.size() * 24 + snap.tracks.size() * 96 + 256);
    char buf[160];

    out += "{\"tick\":";
    out += std::to_string(snap.tick);
    std::snprintf(buf, sizeof(buf), ",\"room\":[%.2f,%.2f]", static_cast<double>(snap.roomSize.x),
                  static_cast<double>(snap.roomSize.y));
    out += buf;

    // Lightweight health strip for the UI header (full detail on /api/status).
    {
        std::lock_guard lock(statsMutex_);
        std::snprintf(buf, sizeof(buf), ",\"health\":{\"tickMs\":%.2f,\"learning\":%s}",
                      static_cast<double>(tickMsAvg_), learning_ ? "true" : "false");
        out += buf;
    }

    // Sensor poses so the UI can draw them.
    out += ",\"sensors\":[";
    const auto layout = sensorLayout();
    for (size_t i = 0; i < layout.size(); ++i) {
        std::snprintf(buf, sizeof(buf), "%s[%.2f,%.2f,%.3f]", i ? "," : "",
                      static_cast<double>(layout[i].position.x),
                      static_cast<double>(layout[i].position.y),
                      static_cast<double>(layout[i].theta));
        out += buf;
    }

    out += "],\"points\":[";
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
                      "\"r\":%.3f,\"coast\":%s,\"age\":%llu}",
                      i ? "," : "", t.id, t.oid, static_cast<double>(t.position.x),
                      static_cast<double>(t.position.y), static_cast<double>(t.velocity.x),
                      static_cast<double>(t.velocity.y), static_cast<double>(t.radius),
                      t.state == TrackState::Coasting ? "true" : "false",
                      static_cast<unsigned long long>(snap.tick - t.bornTick));
        out += buf;
    }
    out += "]}";
    return out;
}

} // namespace sillage
