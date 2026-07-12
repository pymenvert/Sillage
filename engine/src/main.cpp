#include "app/engine.h"

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string_view>

namespace {

sillage::Engine* g_engine = nullptr;

void handleSignal(int) {
    if (g_engine != nullptr) {
        g_engine->stop();
    }
}

void printUsage() {
    std::printf("sillage-engine (M0 walking skeleton)\n\n"
                "  --http-port <port>   UI/API port           (default 8080)\n"
                "  --osc-host <host>    OSC destination       (default 127.0.0.1)\n"
                "  --osc-port <port>    OSC port              (default 12000)\n"
                "  --agents <n>         random walkers        (default 1, + 2 crossing)\n"
                "  --seed <n>           simulator RNG seed    (default 42)\n"
                "  --ticks <n>          run n ticks, then exit (CI/smoke tests)\n"
                "  --headless           no HTTP server\n");
}

} // namespace

int main(int argc, char** argv) {
    sillage::EngineConfig config;

    // The dev UI sits next to the binary (copied at build time).
    config.uiRoot = std::filesystem::path(argv[0]).parent_path() / "ui";

    for (int i = 1; i < argc; ++i) {
        const auto arg = std::string_view(argv[i]);
        const auto next = [&]() -> const char* { return i + 1 < argc ? argv[++i] : ""; };
        if (arg == "--http-port") {
            config.httpPort = static_cast<uint16_t>(std::atoi(next()));
        } else if (arg == "--osc-host") {
            config.oscHost = next();
        } else if (arg == "--osc-port") {
            config.oscPort = static_cast<uint16_t>(std::atoi(next()));
        } else if (arg == "--agents") {
            config.sim.randomAgents = static_cast<uint32_t>(std::atoi(next()));
        } else if (arg == "--seed") {
            config.sim.seed = static_cast<uint32_t>(std::atoi(next()));
        } else if (arg == "--ticks") {
            config.maxTicks = static_cast<uint64_t>(std::atoll(next()));
        } else if (arg == "--headless") {
            config.headless = true;
        } else if (arg == "--help" || arg == "-h") {
            printUsage();
            return 0;
        } else {
            std::fprintf(stderr, "unknown argument: %s\n", std::string(arg).c_str());
            printUsage();
            return 2;
        }
    }

    sillage::Engine engine(config);
    g_engine = &engine;
    std::signal(SIGINT, handleSignal);
    std::signal(SIGTERM, handleSignal);

    return engine.run() ? 0 : 1;
}
