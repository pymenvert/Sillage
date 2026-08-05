#include "app/engine.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <thread>
#include <utility>

namespace sillage {

namespace {

// Escapes a string for the hand-rolled JSON in the WS broadcast: one stray
// quote or backslash in a zone name would otherwise corrupt every frame.
void appendJsonString(std::string& out, const std::string& s) {
    out += '"';
    for (const char c : s) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                char buf[8];
                std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                out += buf;
            } else {
                out += c;
            }
        }
    }
    out += '"';
}

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

// Wiring = which sensors exist and where they connect (count, type, host,
// port). Poses are deliberately excluded: they hot-apply through
// Pipeline::setSensorPoses, while wiring changes need a pipeline rebuild.
bool sensorWiringDiffers(const std::vector<SensorConfig>& a, const std::vector<SensorConfig>& b) {
    if (a.size() != b.size()) {
        return true;
    }
    for (size_t i = 0; i < a.size(); ++i) {
        if (a[i].type != b[i].type || a[i].host != b[i].host || a[i].port != b[i].port) {
            return true;
        }
    }
    return false;
}

} // namespace

std::vector<SensorPose> Engine::sensorLayout() const {
    std::vector<SensorPose> layout;
    if (config_.simEnabled) {
        layout = demoSensorLayout(config_.roomSize);
    }
    for (const SensorConfig& s : config_.sensors) {
        layout.push_back(s.pose);
    }
    return layout;
}

void EngineConfig::applyProject(const ProjectConfig& project) {
    roomSize = project.roomSize;
    simEnabled = project.simEnabled;
    sensors = project.sensors;
    zones = project.zones;
    oscEnabled = project.oscEnabled;
    oscHost = project.oscHost;
    oscPort = project.oscPort;
    tuioEnabled = project.tuioEnabled;
    tuioHost = project.tuioHost;
    tuioPort = project.tuioPort;
    admEnabled = project.admEnabled;
    admHost = project.admHost;
    admPort = project.admPort;
    admMaxObjects = project.admMaxObjects;
    conditioning.predictionSeconds = project.predictionSeconds;
    conditioning.smoothing = project.smoothing;
}

ProjectConfig EngineConfig::toProject() const {
    ProjectConfig project;
    project.roomSize = roomSize;
    project.simEnabled = simEnabled;
    project.sensors = sensors;
    project.zones = zones;
    project.oscEnabled = oscEnabled;
    project.oscHost = oscHost;
    project.oscPort = oscPort;
    project.tuioEnabled = tuioEnabled;
    project.tuioHost = tuioHost;
    project.tuioPort = tuioPort;
    project.admEnabled = admEnabled;
    project.admHost = admHost;
    project.admPort = admPort;
    project.admMaxObjects = admMaxObjects;
    project.predictionSeconds = conditioning.predictionSeconds;
    project.smoothing = conditioning.smoothing;
    return project;
}

Engine::Engine(const EngineConfig& config)
    : config_(config), pipeline_([&] {
          PipelineConfig cfg;
          cfg.sensors = sensorLayout();
          cfg.backgroundBins = 1440; // covers both sim (720 rays) and UST (1081 steps)
          return cfg;
      }()),
      conditioner_(config.conditioning), zoneEngine_(config.zones) {
    project_ = config.toProject();
    if (config_.simEnabled) {
        simulator_ = std::make_unique<Simulator>(
            demoSimParams(config_.roomSize, config_.randomAgents, config_.seed));
        for (const SensorPose& pose : demoSensorLayout(config_.roomSize)) {
            simulator_->addSensor(pose);
        }
    }
    const auto simSensors = static_cast<SensorId>(config_.simEnabled ? 2 : 0);
    for (size_t i = 0; i < config_.sensors.size(); ++i) {
        const SensorConfig& s = config_.sensors[i];
        const SensorId id = simSensors + static_cast<SensorId>(i);
        if (s.type == "hokuyo") {
            HokuyoDriver::Config c;
            c.host = s.host;
            c.port = s.port;
            c.sensorId = id;
            drivers_.push_back(std::make_unique<HokuyoDriver>(std::move(c)));
        } else if (s.type == "sick") {
            SickDriver::Config c;
            c.host = s.host;
            c.port = s.port;
            c.sensorId = id;
            drivers_.push_back(std::make_unique<SickDriver>(std::move(c)));
        } else if (s.type == "udp") {
            UdpBridgeDriver::Config c;
            if (!s.host.empty()) {
                c.bindHost = s.host;
            }
            c.port = s.port;
            c.sensorId = id;
            drivers_.push_back(std::make_unique<UdpBridgeDriver>(std::move(c)));
        } else {
            std::fprintf(stderr, "warning: unknown sensor type '%s' ignored\n",
                         s.type.c_str());
            continue;
        }
        driverSeqs_.push_back(0);
    }
}

bool Engine::run() {
    if (!config_.headless) {
        server_.setApiHandler([this](const std::string& method, const std::string& path,
                                     const std::string& body) {
            return handleApi(method, path, body);
        });
        if (!server_.start(config_.httpBind, config_.httpPort, config_.uiRoot)) {
            std::fprintf(stderr, "error: cannot bind HTTP server on %s:%u\n",
                         config_.httpBind.c_str(), config_.httpPort);
            return false;
        }
        std::printf("UI      : http://%s:%u\n", config_.httpBind.c_str(), config_.httpPort);
    }
    if (config_.oscEnabled) {
        if (!osc_.open(config_.oscHost, config_.oscPort)) {
            std::fprintf(stderr, "error: cannot open OSC destination %s:%u\n",
                         config_.oscHost.c_str(), config_.oscPort);
            return false;
        }
        eventOsc_.open(config_.oscHost, config_.oscPort);
    }
    if (config_.tuioEnabled && !tuio_.open(config_.tuioHost, config_.tuioPort)) {
        std::fprintf(stderr, "error: cannot open TUIO destination\n");
        return false;
    }
    if (config_.admEnabled &&
        !adm_.open(config_.admHost, config_.admPort, config_.admMaxObjects)) {
        std::fprintf(stderr, "error: cannot open ADM-OSC destination\n");
        return false;
    }
    if (!config_.replayPath.empty()) {
        if (!replayer_.open(config_.replayPath)) {
            std::fprintf(stderr, "error: cannot open replay file %s\n",
                         config_.replayPath.string().c_str());
            return false;
        }
        std::printf("Replay  : %s\n", config_.replayPath.string().c_str());
    }
    if (!config_.recordPath.empty()) {
        if (!recorder_.open(config_.recordPath)) {
            std::fprintf(stderr, "error: cannot open record file %s\n",
                         config_.recordPath.string().c_str());
            return false;
        }
        std::printf("Record  : %s\n", config_.recordPath.string().c_str());
    }
    for (auto& driver : drivers_) {
        driver->start();
    }
    std::printf("OSC     : %s:%u (Augmenta legacy)\n", config_.oscHost.c_str(), config_.oscPort);
    std::printf("Sensors : %zu virtual, %zu hokuyo\n",
                static_cast<size_t>(config_.simEnabled ? 2 : 0), drivers_.size());
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
        applyPendingConfig();

        std::vector<ScanFrame> frames;
        if (!config_.replayPath.empty()) {
            auto next = replayer_.nextTick();
            if (!next) {
                std::printf("Replay finished.\n");
                break;
            }
            frames = std::move(next->second);
        } else {
            if (simulator_) {
                frames = simulator_->step(dt, tickStart);
            }
            for (size_t i = 0; i < drivers_.size(); ++i) {
                if (auto frame = drivers_[i]->latestFrame(driverSeqs_[i])) {
                    frames.push_back(std::move(*frame));
                }
            }
        }
        if (recorder_.isOpen()) {
            for (const ScanFrame& frame : frames) {
                recorder_.write(tick, frame);
            }
        }

        FrameSnapshot snap = pipeline_.process(frames, dt, tick, config_.roomSize);
        feedCalibration(snap);
        if (!pipeline_.learning()) {
            // Published copy: conditioning applies to outputs, never to state.
            snap.tracks = conditioner_.apply(snap.tracks, dt);
            // Panic mute: tracking and the UI keep running so the operator can
            // still see what is happening, but nothing reaches the show.
            const bool muted = outputsMuted_.load();
            if (!muted) {
                if (config_.oscEnabled) {
                    osc_.publish(snap);
                }
                if (config_.tuioEnabled) {
                    tuio_.publish(snap);
                }
                if (config_.admEnabled) {
                    adm_.publish(snap);
                }
            }
            // Zone bookkeeping runs either way so occupancy stays truthful and
            // the UI keeps its event feed; only the emission is suppressed.
            for (const ZoneEvent& event : zoneEngine_.update(snap.tracks)) {
                if (!muted && config_.oscEnabled) {
                    osc::Message msg("/sillage/zone/" + osc::sanitizeAddressPart(event.zone) +
                                     "/" +
                                     (event.type == ZoneEvent::Type::Enter ? "enter" : "exit"));
                    msg.addInt32(static_cast<int32_t>(event.trackId));
                    const auto bytes = msg.encode();
                    eventOsc_.send(bytes.data(), bytes.size());
                }
                pendingEvents_.push_back(event);
            }
        }

        const float tickMs =
            std::chrono::duration<float, std::milli>(Clock::now() - tickStart).count();
        {
            std::lock_guard lock(statsMutex_);
            tickMsAvg_ = 0.98f * tickMsAvg_ + 0.02f * tickMs;
            tickMsMax_ = std::max(tickMsMax_ * 0.999f, tickMs); // decaying peak
            tracksNow_ = static_cast<uint32_t>(snap.tracks.size());
            learning_ = pipeline_.learning();
            // Sensor health lands in every WS frame (not just the 2 s /api/status
            // poll) so the UI can raise an alarm the operator sees immediately.
            sensorsTotal_ = static_cast<uint32_t>(drivers_.size());
            uint32_t down = 0;
            for (const auto& driver : drivers_) {
                if (!driver->health().connected) {
                    ++down;
                }
            }
            sensorsDown_ = down;
            // Latched, never cleared: a truncated .srec is unusable evidence,
            // so the operator must learn about it during the show, not the
            // morning after when the recording is needed.
            recordingFailed_ = recordingFailed_ || recorder_.failed();
        }

        if (!config_.headless && tick % 2 == 0) { // UI at ~30 Hz
            server_.broadcast(snapshotToJson(snap));
        }

        ++tick;
        if (config_.maxTicks && tick >= *config_.maxTicks) {
            break;
        }
        nextTick += tickPeriod;
        // Catch-up clamp: after an overrun (a stalled disk write, a laptop
        // resuming from sleep, an OS hiccup) nextTick can sit far in the past.
        // Without this the loop would run flat out with no sleep, burning a
        // core and flooding every OSC destination with a burst of frames.
        // Dropping the backlog keeps real-time pacing; we never replay history.
        const auto now = Clock::now();
        if (nextTick < now) {
            nextTick = now + tickPeriod;
            std::lock_guard lock(statsMutex_);
            ++overruns_;
        }
        std::this_thread::sleep_until(nextTick);
    }
    for (auto& driver : drivers_) {
        driver->stop();
    }
    server_.stop();
    return true;
}

std::string Engine::handleApi(const std::string& method, const std::string& path,
                              const std::string& body) {
    if (method == "GET" && path == "/api/status") {
        return statusJson();
    }
    if (method == "GET" && path == "/api/config") {
        std::lock_guard lock(configMutex_);
        return project_.toJson().serialize(2);
    }
    // Show controls. Deliberately tiny and always available — these are the
    // buttons an operator hits under pressure, in the dark.
    if (method == "POST" && path == "/api/show/lock") {
        showLocked_ = true;
        return "{\"ok\":true,\"showLocked\":true}";
    }
    if (method == "POST" && path == "/api/show/unlock") {
        showLocked_ = false;
        return "{\"ok\":true,\"showLocked\":false}";
    }
    if (method == "POST" && path == "/api/show/mute") {
        outputsMuted_ = true;
        return "{\"ok\":true,\"muted\":true}";
    }
    if (method == "POST" && path == "/api/show/unmute") {
        outputsMuted_ = false;
        return "{\"ok\":true,\"muted\":false}";
    }
    if (method == "POST" && path == "/api/background/relearn") {
        if (showLocked_.load()) {
            return "{\"ok\":false,\"error\":\"show lock active\"}";
        }
        // Deferred to the tick thread: this runs on a connection thread, and
        // the background models are read concurrently by the tick.
        pipeline_.requestRelearn();
        return "{\"ok\":true,\"relearning\":true}";
    }

    if (method == "POST" && path == "/api/config") {
        if (showLocked_.load()) {
            return "{\"ok\":false,\"error\":\"show lock active: configuration is read-only\"}";
        }
        const auto parsed = json::parse(body);
        if (!parsed.value) {
            // The parser error is embedded in a JSON string, so escape it —
            // a quote in the message would otherwise emit invalid JSON.
            std::string out = "{\"ok\":false,\"error\":";
            appendJsonString(out, "invalid JSON: " + parsed.error);
            return out + "}";
        }
        std::string error;
        auto incoming = ProjectConfig::fromJson(*parsed.value, error);
        if (!incoming) {
            std::string out = "{\"ok\":false,\"error\":";
            appendJsonString(out, error);
            return out + "}";
        }
        // Writers are serialized end to end (compare, persist, publish) so two
        // concurrent POSTs cannot leave the file on one version and the
        // running engine on the other. configMutex_ itself is only taken for
        // the short shared-state accesses: the disk write used to happen under
        // it, and a slow save (project on a network share) stalled the tick
        // thread, which takes configMutex_ every tick.
        std::lock_guard writer(configWriteMutex_);
        bool restartRequired;
        {
            std::lock_guard lock(configMutex_);
            // Zones, outputs, conditioning AND sensor poses hot-apply at the
            // next tick — the background is per-sensor polar, so a pose change
            // invalidates nothing (see Pipeline::setSensorPoses). Only the
            // wiring (which sensors exist, where they connect), the room and
            // the sim flag need a pipeline rebuild, i.e. a restart. Compare
            // wiring in full (not just count): editing a sensor's host must
            // still report restartRequired, or disk and runtime silently
            // diverge.
            restartRequired = incoming->roomSize.x != project_.roomSize.x ||
                              incoming->roomSize.y != project_.roomSize.y ||
                              incoming->simEnabled != project_.simEnabled ||
                              sensorWiringDiffers(incoming->sensors, project_.sensors);
        }
        if (!config_.projectPath.empty()) { // projectPath is fixed at startup
            std::string saveError;
            if (!incoming->save(config_.projectPath, saveError)) {
                std::string out = "{\"ok\":false,\"error\":";
                appendJsonString(out, saveError);
                return out + "}";
            }
        }
        {
            std::lock_guard lock(configMutex_);
            pendingConfig_ = std::move(*incoming);
        }
        return std::string("{\"ok\":true,\"restartRequired\":") +
               (restartRequired ? "true" : "false") +
               ",\"persisted\":" + (config_.projectPath.empty() ? "false" : "true") + "}";
    }
    if (path.rfind("/api/calib/", 0) == 0) {
        return handleCalibApi(method, path, body);
    }
    return {};
}

// Walk-based auto-calibration over HTTP (docs/04): start collecting, watch
// the observation counts grow while one person walks the overlaps, solve,
// then apply. Collection happens on the tick thread (feedCalibration); these
// handlers run on connection threads and only touch the collector under
// calibMutex_. solve() runs on a COPY so its RANSAC never blocks a tick nor
// races the tick thread's addObservation.
std::string Engine::handleCalibApi(const std::string& method, const std::string& path,
                                   const std::string& body) {
    if (method == "POST" && path == "/api/calib/start") {
        if (showLocked_.load()) {
            return "{\"ok\":false,\"error\":\"show lock active\"}";
        }
        size_t sensorCount;
        {
            std::lock_guard lock(configMutex_);
            sensorCount = sensorLayout().size();
        }
        std::lock_guard lock(calibMutex_);
        calibCollector_ = std::make_unique<CalibrationCollector>(sensorCount);
        calibCollecting_ = true;
        calibResults_.clear();
        return "{\"ok\":true,\"collecting\":true}";
    }
    if (method == "POST" && path == "/api/calib/stop") {
        std::lock_guard lock(calibMutex_);
        calibCollecting_ = false;
        return "{\"ok\":true,\"collecting\":false}";
    }
    if (method == "GET" && path == "/api/calib/status") {
        std::lock_guard lock(calibMutex_);
        std::string out = std::string("{\"collecting\":") + (calibCollecting_ ? "true" : "false") +
                          ",\"observations\":[";
        if (calibCollector_) {
            size_t count;
            {
                std::lock_guard cfg(configMutex_);
                count = sensorLayout().size();
            }
            for (size_t s = 0; s < count; ++s) {
                out += (s ? "," : "") +
                       std::to_string(calibCollector_->observationCount(static_cast<SensorId>(s)));
            }
        }
        out += std::string("],\"solved\":") + (calibResults_.empty() ? "false" : "true") + "}";
        return out;
    }
    if (method == "POST" && path == "/api/calib/solve") {
        size_t anchor = 0;
        if (!body.empty()) {
            const auto parsed = json::parse(body);
            if (parsed.value) {
                anchor = static_cast<size_t>((*parsed.value)["anchor"].asNumber(0.0));
            }
        }
        std::vector<SensorPose> layout;
        {
            std::lock_guard lock(configMutex_);
            layout = sensorLayout();
        }
        if (anchor >= layout.size()) {
            return "{\"ok\":false,\"error\":\"anchor out of range\"}";
        }
        std::optional<CalibrationCollector> copy;
        {
            std::lock_guard lock(calibMutex_);
            if (!calibCollector_) {
                return "{\"ok\":false,\"error\":\"start collection first\"}";
            }
            copy = *calibCollector_; // solve outside the lock, on the copy
        }
        // Anchored to the anchor sensor's CURRENT believed pose, so solved
        // poses land in the room frame the operator already works in.
        auto results = copy->solve(static_cast<SensorId>(anchor), layout[anchor]);
        std::string out = "{\"ok\":true,\"results\":[";
        char buf[160];
        for (size_t s = 0; s < results.size(); ++s) {
            const auto& r = results[s];
            std::snprintf(buf, sizeof(buf),
                          "%s{\"sensor\":%zu,\"solved\":%s,\"x\":%.4f,\"y\":%.4f,"
                          "\"theta\":%.5f,\"rmse\":%.4f,\"pairs\":%u,\"message\":",
                          s ? "," : "", s, r.solved ? "true" : "false",
                          static_cast<double>(r.pose.position.x),
                          static_cast<double>(r.pose.position.y),
                          static_cast<double>(r.pose.theta), static_cast<double>(r.rmse),
                          r.pairs);
            out += buf;
            appendJsonString(out, r.message);
            out += '}';
        }
        {
            std::lock_guard lock(calibMutex_);
            calibResults_ = std::move(results);
        }
        return out + "]}";
    }
    if (method == "POST" && path == "/api/calib/apply") {
        if (showLocked_.load()) {
            return "{\"ok\":false,\"error\":\"show lock active\"}";
        }
        std::vector<CalibrationCollector::SensorResult> results;
        {
            std::lock_guard lock(calibMutex_);
            if (calibResults_.empty()) {
                return "{\"ok\":false,\"error\":\"solve first\"}";
            }
            results = calibResults_;
        }
        // Solved poses go through the ordinary pendingConfig_ path: persisted
        // like any config edit, hot-applied at the next tick (a pose change is
        // not a wiring change). Demo-mode sim sensors occupy the first layout
        // slots but have no project entry to persist — only real sensors are
        // applied; index j in the project maps to layout slot simSensors + j.
        std::lock_guard writer(configWriteMutex_);
        ProjectConfig incoming;
        size_t applied = 0;
        {
            std::lock_guard lock(configMutex_);
            incoming = project_;
            const size_t simSensors = config_.simEnabled ? 2 : 0;
            for (size_t j = 0; j < incoming.sensors.size(); ++j) {
                const size_t slot = simSensors + j;
                if (slot < results.size() && results[slot].solved) {
                    incoming.sensors[j].pose = results[slot].pose;
                    ++applied;
                }
            }
        }
        if (applied == 0) {
            return "{\"ok\":true,\"applied\":0}";
        }
        if (!config_.projectPath.empty()) {
            std::string saveError;
            if (!incoming.save(config_.projectPath, saveError)) {
                std::string out = "{\"ok\":false,\"error\":";
                appendJsonString(out, saveError);
                return out + "}";
            }
        }
        {
            std::lock_guard lock(configMutex_);
            pendingConfig_ = std::move(incoming);
        }
        return "{\"ok\":true,\"applied\":" + std::to_string(applied) +
               ",\"persisted\":" + (config_.projectPath.empty() ? "false" : "true") + "}";
    }
    return {};
}

// Tick thread: while collecting, rebuild each sensor's local observation of
// the walker from the fused room-frame foreground. toLocal inverts the exact
// transform fusion applied, so the collector sees true sensor-local data even
// while the pipeline is running on poses that are wrong — which is the whole
// point: calibration runs BEFORE the poses are right.
void Engine::feedCalibration(const FrameSnapshot& snap) {
    std::lock_guard lock(calibMutex_);
    if (!calibCollecting_ || !calibCollector_) {
        return;
    }
    const auto layout = sensorLayout(); // tick thread owns these poses
    std::vector<std::vector<Vec2>> local(layout.size());
    for (const WorldPoint& p : snap.foreground) {
        if (p.sensor < layout.size()) {
            local[p.sensor].push_back(layout[p.sensor].toLocal(p.pos));
        }
    }
    for (size_t s = 0; s < local.size(); ++s) {
        calibCollector_->addObservation(static_cast<SensorId>(s), snap.tick, local[s]);
    }
}

void Engine::applyPendingConfig() {
    std::optional<ProjectConfig> pending;
    {
        std::lock_guard lock(configMutex_);
        if (!pendingConfig_) {
            return;
        }
        pending = std::move(pendingConfig_);
        pendingConfig_.reset();
    }
    // Hot-appliable parts only; geometry changes wait for a restart.
    zoneEngine_ = ZoneEngine(pending->zones);
    pendingEvents_.clear();
    conditioner_ = OutputConditioner(
        {.predictionSeconds = pending->predictionSeconds, .smoothing = pending->smoothing});

    if (pending->oscEnabled &&
        (!config_.oscEnabled || pending->oscHost != config_.oscHost ||
         pending->oscPort != config_.oscPort)) {
        osc_.open(pending->oscHost, pending->oscPort);
        eventOsc_.open(pending->oscHost, pending->oscPort);
    }
    if (pending->tuioEnabled &&
        (!config_.tuioEnabled || pending->tuioHost != config_.tuioHost ||
         pending->tuioPort != config_.tuioPort)) {
        tuio_.open(pending->tuioHost, pending->tuioPort);
    }
    if (pending->admEnabled &&
        (!config_.admEnabled || pending->admHost != config_.admHost ||
         pending->admPort != config_.admPort)) {
        adm_.open(pending->admHost, pending->admPort, pending->admMaxObjects);
    }
    config_.oscEnabled = pending->oscEnabled;
    config_.oscHost = pending->oscHost;
    config_.oscPort = pending->oscPort;
    config_.tuioEnabled = pending->tuioEnabled;
    config_.tuioHost = pending->tuioHost;
    config_.tuioPort = pending->tuioPort;
    config_.admEnabled = pending->admEnabled;
    config_.admHost = pending->admHost;
    config_.admPort = pending->admPort;
    config_.admMaxObjects = pending->admMaxObjects;
    config_.zones = pending->zones;
    config_.conditioning.predictionSeconds = pending->predictionSeconds;
    config_.conditioning.smoothing = pending->smoothing;

    std::lock_guard lock(configMutex_);
    // Sensor poses hot-apply when the wiring is unchanged: this runs on the
    // tick thread, the same thread that fuses through the poses, and the
    // per-sensor polar background survives a pose change untouched. This is
    // the path a calibration workflow needs — "adjust the pose" must never
    // mean "restart the engine and re-learn the background in a room that is
    // no longer empty". A wiring change (sensor added/removed/re-addressed)
    // keeps the old runtime sensors until restart, as before. Under
    // configMutex_ because the calibration API reads these poses from
    // connection threads.
    const bool wiringUnchanged = !sensorWiringDiffers(pending->sensors, config_.sensors);
    if (wiringUnchanged) {
        for (size_t i = 0; i < config_.sensors.size(); ++i) {
            config_.sensors[i].pose = pending->sensors[i].pose;
        }
        pipeline_.setSensorPoses(sensorLayout());
    }
    // Runtime geometry stays as-is until restart; record the rest as truth.
    // With unchanged wiring the pending sensors ARE the runtime sensors
    // (poses just hot-applied above), so they stay; otherwise the old ones
    // are restored so disk and runtime cannot silently diverge.
    const Vec2 room = project_.roomSize;
    const bool sim = project_.simEnabled;
    auto sensors = project_.sensors;
    project_ = std::move(*pending);
    project_.roomSize = room;
    project_.simEnabled = sim;
    if (!wiringUnchanged) {
        project_.sensors = std::move(sensors);
    }
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
                  SILLAGE_VERSION, static_cast<double>(avg), static_cast<double>(mx), tracks,
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
    for (size_t i = 0; i < drivers_.size(); ++i) {
        const SensorHealth h = drivers_[i]->health();
        std::snprintf(buf, sizeof(buf),
                      "%s{\"id\":%zu,\"type\":\"%s\",\"connected\":%s,\"fps\":%.1f,"
                      "\"frames\":%llu,\"errors\":%llu,\"lastError\":",
                      first ? "" : ",", simSensors + i, drivers_[i]->type(),
                      h.connected ? "true" : "false",
                      static_cast<double>(h.scansPerSecond),
                      static_cast<unsigned long long>(h.framesReceived),
                      static_cast<unsigned long long>(h.decodeErrors));
        out += buf;
        // Every driver fills lastError; dropping it here left the integrator
        // staring at "412 decode errors" with no way to learn which error.
        // Escaped separately (not snprintf'd): driver messages embed peer
        // strings of unbounded length.
        appendJsonString(out, h.lastError);
        out += '}';
        first = false;
    }
    bool recFailed;
    {
        std::lock_guard lock(statsMutex_);
        recFailed = recordingFailed_;
    }
    out += std::string("],\"recordingFailed\":") + (recFailed ? "true" : "false") + "}";
    return out;
}

std::string Engine::snapshotToJson(const FrameSnapshot& snap) {
    // Hand-rolled JSON: schema lives in protocol/ once the real API server
    // lands; M0/M1 keep the engine dependency-free.
    std::string out;
    out.reserve(snap.foreground.size() * 24 + snap.tracks.size() * 96 + 256);
    char buf[224]; // sized for the health strip below, its longest tenant

    out += "{\"tick\":";
    out += std::to_string(snap.tick);
    std::snprintf(buf, sizeof(buf), ",\"room\":[%.2f,%.2f]", static_cast<double>(snap.roomSize.x),
                  static_cast<double>(snap.roomSize.y));
    out += buf;

    // Lightweight health strip for the UI header (full detail on /api/status).
    {
        std::lock_guard lock(statsMutex_);
        std::snprintf(buf, sizeof(buf),
                      ",\"health\":{\"tickMs\":%.2f,\"learning\":%s,\"locked\":%s,"
                      "\"muted\":%s,\"overruns\":%llu,\"sensorsDown\":%u,\"sensorsTotal\":%u,"
                      "\"recFailed\":%s}",
                      static_cast<double>(tickMsAvg_), learning_ ? "true" : "false",
                      showLocked_.load() ? "true" : "false",
                      outputsMuted_.load() ? "true" : "false",
                      static_cast<unsigned long long>(overruns_), sensorsDown_, sensorsTotal_,
                      recordingFailed_ ? "true" : "false");
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

    // Zones with occupancy + the events since the previous broadcast.
    out += "],\"zones\":[";
    const auto zoneStatus = zoneEngine_.status();
    for (size_t z = 0; z < zoneStatus.size(); ++z) {
        const auto& s = zoneStatus[z];
        out += z ? "," : "";
        out += "{\"name\":";
        appendJsonString(out, s.zone->name);
        out += ",\"occ\":" + std::to_string(s.occupants) +
               ",\"entries\":" + std::to_string(s.totalEntries) + ",\"poly\":[";
        for (size_t p = 0; p < s.zone->polygon.size(); ++p) {
            std::snprintf(buf, sizeof(buf), "%s[%.2f,%.2f]", p ? "," : "",
                          static_cast<double>(s.zone->polygon[p].x),
                          static_cast<double>(s.zone->polygon[p].y));
            out += buf;
        }
        out += "]}";
    }
    out += "],\"events\":[";
    for (size_t e = 0; e < pendingEvents_.size(); ++e) {
        const ZoneEvent& ev = pendingEvents_[e];
        out += e ? "," : "";
        out += "{\"zone\":";
        appendJsonString(out, ev.zone);
        out += ",\"type\":\"" + std::string(ev.type == ZoneEvent::Type::Enter ? "enter" : "exit") +
               "\",\"id\":" + std::to_string(ev.trackId) + "}";
    }
    pendingEvents_.clear();

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
