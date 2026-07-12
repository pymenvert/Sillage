#include "app/engine.h"
#include "eval/scenarios.h"

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
    std::printf("sillage-engine\n\n"
                "  --http-port <port>   UI/API port           (default 8080)\n"
                "  --osc-host <host>    OSC destination       (default 127.0.0.1)\n"
                "  --osc-port <port>    OSC port              (default 12000)\n"
                "  --agents <n>         random walkers        (default 1, + 2 crossing)\n"
                "  --seed <n>           simulator RNG seed    (default 42)\n"
                "  --ticks <n>          run n ticks, then exit (CI/smoke tests)\n"
                "  --headless           no HTTP server\n"
                "  --eval               run the MOT scenario library and exit\n");
}

// Runs the full scenario library, prints a metrics table, returns the number
// of failed scenarios (CI gate: exit code 0 = all green).
int runEval() {
    std::printf("%-20s %8s %8s %8s %7s %7s %5s %5s  %s\n", "scenario", "IDsw", "MOTA", "IDF1",
                "miss", "FP", "ids", "maxT", "verdict");
    int failed = 0;
    for (const sillage::Scenario& scenario : sillage::scenarioLibrary()) {
        const sillage::ScenarioOutcome outcome = sillage::runScenario(scenario);
        const sillage::MotResult& m = outcome.metrics;
        const char* verdict = outcome.passed          ? "PASS"
                              : scenario.quarantined ? "QUARANTINE "
                                                      : "FAIL ";
        std::printf("%-20s %8d %8.3f %8.3f %7d %7d %5d %5d  %s%s\n", scenario.name.c_str(),
                    m.idSwitches, static_cast<double>(m.mota), static_cast<double>(m.idf1),
                    m.misses, m.falsePositives, m.distinctIds, m.maxSimultaneous, verdict,
                    outcome.passed ? "" : outcome.failureReason.c_str());
        if (!outcome.passed && !scenario.quarantined) {
            ++failed;
        }
    }
    return failed;
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
            config.randomAgents = static_cast<uint32_t>(std::atoi(next()));
        } else if (arg == "--seed") {
            config.seed = static_cast<uint32_t>(std::atoi(next()));
        } else if (arg == "--ticks") {
            config.maxTicks = static_cast<uint64_t>(std::atoll(next()));
        } else if (arg == "--headless") {
            config.headless = true;
        } else if (arg == "--eval") {
            return runEval() == 0 ? 0 : 1;
        } else if (arg == "--debug-scenario") {
            const std::string name = next();
            for (const sillage::Scenario& s : sillage::scenarioLibrary()) {
                if (s.name == name) {
                    const auto outcome = sillage::runScenario(s, 60.0f, true);
                    std::printf("%s: %s %s\n", name.c_str(),
                                outcome.passed ? "PASS" : "FAIL", outcome.failureReason.c_str());
                    return outcome.passed ? 0 : 1;
                }
            }
            std::fprintf(stderr, "unknown scenario: %s\n", name.c_str());
            return 2;
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
