#pragma once

#include "core/background.h"
#include "core/types.h"
#include "detect/clustering.h"
#include "io/augmenta_osc.h"
#include "net/ws_server.h"
#include "sim/simulator.h"
#include "track/tracker.h"

#include <atomic>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace sillage {

struct EngineConfig {
    float tickHz = 60.0f;
    std::string httpBind = "127.0.0.1";
    uint16_t httpPort = 8080;
    std::string oscHost = "127.0.0.1";
    uint16_t oscPort = 12000;
    std::filesystem::path uiRoot = "ui";
    Simulator::Params sim{};
    ClusteringParams clustering{};
    TrackerParams tracker{};
    uint32_t backgroundLearnFrames = 60;
    bool headless = false; // no HTTP server (tests, benchmarks)
    std::optional<uint64_t> maxTicks; // run N ticks then stop (tests/CI)
};

// M0 walking skeleton: simulator -> background -> clustering -> tracker ->
// OSC + WebSocket. Real sensor drivers replace the simulator in M1 behind the
// same pipeline.
class Engine {
public:
    explicit Engine(EngineConfig config);

    // Blocks until stop() (or maxTicks). Returns false on startup failure.
    bool run();
    void stop() { running_ = false; }

    const FrameSnapshot& lastSnapshot() const { return snapshot_; }

private:
    void tickOnce(float dt, uint64_t tick);
    std::string snapshotToJson(const FrameSnapshot& snap) const;

    EngineConfig config_;
    Simulator simulator_;
    std::vector<BackgroundModel> backgrounds_; // one per sensor
    Tracker tracker_;
    AugmentaOscOutput osc_;
    net::WsHttpServer server_;
    std::atomic<bool> running_{false};
    FrameSnapshot snapshot_;
};

} // namespace sillage
