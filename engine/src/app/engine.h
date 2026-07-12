#pragma once

#include "core/types.h"
#include "drivers/hokuyo/hokuyo_driver.h"
#include "io/augmenta_osc.h"
#include "net/ws_server.h"
#include "pipeline/pipeline.h"
#include "record/recorder.h"
#include "sim/simulator.h"

#include <atomic>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace sillage {

struct HokuyoSensorConfig {
    std::string host;
    uint16_t port = 10940;
    SensorPose pose{};
};

struct EngineConfig {
    float tickHz = 60.0f;
    std::string httpBind = "127.0.0.1";
    uint16_t httpPort = 8080;
    std::string oscHost = "127.0.0.1";
    uint16_t oscPort = 12000;
    std::filesystem::path uiRoot = "ui";
    Vec2 roomSize{10.0f, 8.0f};
    bool simEnabled = true;    // virtual sensors + agents (demo mode)
    uint32_t randomAgents = 1; // demo walkers besides the two crossing agents
    uint32_t seed = 42;
    std::vector<HokuyoSensorConfig> hokuyos; // real sensors (appended after sim)
    std::filesystem::path recordPath; // non-empty: record raw scans (.srec)
    std::filesystem::path replayPath; // non-empty: replay instead of sensors
    bool headless = false;            // no HTTP server (tests, benchmarks)
    std::optional<uint64_t> maxTicks; // run N ticks then stop (tests/CI)
};

// Live engine: sensors (simulator and/or Hokuyo drivers) -> Pipeline ->
// OSC + WebSocket UI + health/status reporting.
class Engine {
public:
    explicit Engine(const EngineConfig& config);

    // Blocks until stop() (or maxTicks). Returns false on startup failure.
    bool run();
    void stop() { running_ = false; }

private:
    std::vector<SensorPose> sensorLayout() const;
    std::string snapshotToJson(const FrameSnapshot& snap) const;
    std::string statusJson() const;

    EngineConfig config_;
    std::unique_ptr<Simulator> simulator_; // null when simEnabled is false
    std::vector<std::unique_ptr<HokuyoDriver>> hokuyos_;
    std::vector<uint64_t> hokuyoSeqs_;
    Pipeline pipeline_;
    AugmentaOscOutput osc_;
    net::WsHttpServer server_;
    ScanRecorder recorder_;
    ScanReplayer replayer_;
    std::atomic<bool> running_{false};

    // Load stats, published on /api/status and the WS health payload.
    mutable std::mutex statsMutex_;
    float tickMsAvg_ = 0.0f;
    float tickMsMax_ = 0.0f;
    uint32_t tracksNow_ = 0;
    bool learning_ = true;
};

} // namespace sillage
