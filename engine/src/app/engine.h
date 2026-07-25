#pragma once

#include "core/types.h"
#include "drivers/hokuyo/hokuyo_driver.h"
#include "config/project.h"
#include "drivers/driver.h"
#include "drivers/sick/sick_driver.h"
#include "drivers/udpbridge/udp_bridge.h"
#include "io/augmenta_osc.h"
#include "io/ecosystem_outputs.h"
#include "logic/zones.h"
#include "net/ws_server.h"
#include "pipeline/pipeline.h"
#include "record/recorder.h"
#include "sim/simulator.h"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace sillage {

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
    std::vector<SensorConfig> sensors; // real sensors, any driver type (after sim)
    std::vector<ZoneConfig> zones;
    bool oscEnabled = true;
    bool tuioEnabled = false;
    std::string tuioHost = "127.0.0.1";
    uint16_t tuioPort = 3333;
    bool admEnabled = false;
    std::string admHost = "127.0.0.1";
    uint16_t admPort = 4001;
    uint32_t admMaxObjects = 16;
    OutputConditioner::Params conditioning{};
    std::filesystem::path recordPath; // non-empty: record raw scans (.srec)
    std::filesystem::path replayPath; // non-empty: replay instead of sensors
    bool headless = false;            // no HTTP server (tests, benchmarks)
    std::optional<uint64_t> maxTicks; // run N ticks then stop (tests/CI)

    std::filesystem::path projectPath; // where POST /api/config persists

    // Applies the persistent project file onto this config (CLI overrides win
    // because they are parsed afterwards); and the reverse mapping.
    void applyProject(const ProjectConfig& project);
    ProjectConfig toProject() const;
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
    std::string snapshotToJson(const FrameSnapshot& snap); // drains pending events
    std::string statusJson() const;
    std::string handleApi(const std::string& method, const std::string& path,
                          const std::string& body);
    void applyPendingConfig(); // called at tick boundary only

    EngineConfig config_;
    std::unique_ptr<Simulator> simulator_; // null when simEnabled is false
    std::vector<std::unique_ptr<ISensorDriver>> drivers_;
    std::vector<uint64_t> driverSeqs_;
    Pipeline pipeline_;
    AugmentaOscOutput osc_;
    TuioOutput tuio_;
    AdmOscOutput adm_;
    net::UdpSender eventOsc_; // /sillage/zone/* messages
    OutputConditioner conditioner_;
    ZoneEngine zoneEngine_;
    std::vector<ZoneEvent> pendingEvents_; // between two UI broadcasts
    net::WsHttpServer server_;
    ScanRecorder recorder_;
    ScanReplayer replayer_;
    std::atomic<bool> running_{false};

    // Live project state: read by the API thread, swapped at tick boundaries.
    std::mutex configMutex_;
    ProjectConfig project_;
    std::optional<ProjectConfig> pendingConfig_;

    // Show mode (docs/11): while armed, the configuration is read-only so a
    // stray click or an errant POST cannot alter zones or outputs during a
    // performance. `muted` is the panic switch: the engine keeps tracking and
    // the UI keeps updating, but nothing is emitted downstream.
    std::atomic<bool> showLocked_{false};
    std::atomic<bool> outputsMuted_{false};

    // Load stats, published on /api/status and the WS health payload.
    mutable std::mutex statsMutex_;
    uint64_t overruns_ = 0;
    uint32_t sensorsDown_ = 0;  // hardware drivers reporting disconnected
    uint32_t sensorsTotal_ = 0; // hardware drivers configured
    float tickMsAvg_ = 0.0f;
    float tickMsMax_ = 0.0f;
    uint32_t tracksNow_ = 0;
    bool learning_ = true;
};

} // namespace sillage
