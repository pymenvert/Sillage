#pragma once

#include "calib/collector.h"
#include "core/frame_hold.h"
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
    std::string handleCalibApi(const std::string& method, const std::string& path,
                               const std::string& body);
    void applyPendingConfig(); // called at tick boundary only
    // Tick thread, per tick. Only sensors that delivered a FRESH frame this
    // tick feed the collector: a held (stale) scan of a moving walker would
    // pair a stale center against another sensor's fresh one and eat into
    // the RANSAC error budget for no gain.
    void feedCalibration(const FrameSnapshot& snap, const std::vector<bool>& freshSensor);

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
    FrameHold frameHold_; // slow sensors contribute every tick, bounded by age
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

    // Walk-based auto-calibration (docs/04): the collector accumulates the
    // walker's per-sensor observations on the tick thread; the HTTP thread
    // starts/stops collection, solves on a COPY (so a multi-hundred-ms RANSAC
    // never blocks a tick or races addObservation), and applies solved poses
    // through the ordinary pendingConfig_ path — where they hot-apply, since
    // a pose change is not a wiring change.
    std::mutex calibMutex_;
    std::unique_ptr<CalibrationCollector> calibCollector_; // null until start
    bool calibCollecting_ = false;
    std::vector<CalibrationCollector::SensorResult> calibResults_; // last solve

    // Serializes POST /api/config writers end to end (persist to disk, then
    // publish to the tick thread), so two concurrent writers cannot leave the
    // file and the running engine on different versions. Deliberately separate
    // from configMutex_: the disk write happens under this mutex only, so a
    // slow save (project on a network share) can no longer stall the tick
    // thread, which grabs configMutex_ every tick to check for pending config.
    std::mutex configWriteMutex_;

    // Load stats, published on /api/status and the WS health payload.
    mutable std::mutex statsMutex_;
    uint64_t overruns_ = 0;
    uint32_t sensorsDown_ = 0;  // hardware drivers reporting disconnected
    uint32_t sensorsTotal_ = 0; // hardware drivers configured
    float tickMsAvg_ = 0.0f;
    float tickMsMax_ = 0.0f;
    uint32_t tracksNow_ = 0;
    bool learning_ = true;
    bool recordingFailed_ = false; // a .srec write failed (disk full); latched
};

} // namespace sillage
