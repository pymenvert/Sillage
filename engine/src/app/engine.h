#pragma once

#include "core/types.h"
#include "io/augmenta_osc.h"
#include "net/ws_server.h"
#include "pipeline/pipeline.h"
#include "sim/simulator.h"

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
    uint32_t randomAgents = 1; // demo walkers besides the two crossing agents
    uint32_t seed = 42;
    bool headless = false;            // no HTTP server (tests, benchmarks)
    std::optional<uint64_t> maxTicks; // run N ticks then stop (tests/CI)
};

// Live engine: simulator (real drivers land with M1 hardware) -> Pipeline ->
// OSC + WebSocket UI.
class Engine {
public:
    explicit Engine(const EngineConfig& config);

    // Blocks until stop() (or maxTicks). Returns false on startup failure.
    bool run();
    void stop() { running_ = false; }

private:
    std::string snapshotToJson(const FrameSnapshot& snap) const;

    EngineConfig config_;
    Simulator simulator_;
    Pipeline pipeline_;
    AugmentaOscOutput osc_;
    net::WsHttpServer server_;
    std::atomic<bool> running_{false};
};

} // namespace sillage
