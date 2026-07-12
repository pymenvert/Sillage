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
                "  --http-bind <addr>   UI/API bind address   (default 127.0.0.1; 0.0.0.0 = LAN)\n"
                "  --osc-host <host>    OSC destination       (default 127.0.0.1)\n"
                "  --osc-port <port>    OSC port              (default 12000)\n"
                "  --agents <n>         random walkers        (default 1, + 2 crossing)\n"
                "  --seed <n>           simulator RNG seed    (default 42)\n"
                "  --room <WxH>         room size in meters   (default 10x8)\n"
                "  --hokuyo <host[:port][@x,y,theta]>  add a Hokuyo URG/UST (SCIP 2.2)\n"
                "  --sick <host[:port][@x,y,theta]>    add a SICK TiM (CoLa A)\n"
                "  --udp-sensor <port[@x,y,theta]>     add a UDP bridge sensor (any hardware)\n"
                "  --no-sim             disable the demo simulator (real sensors only)\n"
                "  --config <file>      load a project file (CLI flags override)\n"
                "  --save-config <file> write the current config as a project file, exit\n"
                "  --ticks <n>          run n ticks, then exit (CI/smoke tests)\n"
                "  --record <file>      record raw scans to a .srec file\n"
                "  --replay <file>      replay a .srec file instead of sensors\n"
                "  --headless           no HTTP server\n"
                "  --eval               run the MOT scenario library and exit\n");
}

// Runs the full scenario library, prints a metrics table, returns the number
// of failed scenarios (CI gate: exit code 0 = all green).
int runEval() {
    std::printf("%-20s %8s %8s %8s %7s %7s %5s %5s %8s  %s\n", "scenario", "IDsw", "MOTA",
                "IDF1", "miss", "FP", "ids", "maxT", "tick(us)", "verdict");
    int failed = 0;
    for (const sillage::Scenario& scenario : sillage::scenarioLibrary()) {
        const sillage::ScenarioOutcome outcome = sillage::runScenario(scenario);
        const sillage::MotResult& m = outcome.metrics;
        const char* verdict = outcome.passed          ? "PASS"
                              : scenario.quarantined ? "QUARANTINE "
                                                      : "FAIL ";
        std::printf("%-20s %8d %8.3f %8.3f %7d %7d %5d %5d %8.0f  %s%s\n", scenario.name.c_str(),
                    m.idSwitches, static_cast<double>(m.mota), static_cast<double>(m.idf1),
                    m.misses, m.falsePositives, m.distinctIds, m.maxSimultaneous,
                    static_cast<double>(outcome.avgTickUs), verdict,
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

    // Pass 1: the project file loads first so every CLI flag can override it.
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::string_view(argv[i]) == "--config") {
            std::string error;
            const auto project = sillage::ProjectConfig::load(argv[i + 1], error);
            if (!project) {
                std::fprintf(stderr, "config error: %s\n", error.c_str());
                return 2;
            }
            config.applyProject(*project);
            config.projectPath = argv[i + 1]; // POST /api/config persists here
        }
    }

    for (int i = 1; i < argc; ++i) {
        const auto arg = std::string_view(argv[i]);
        const auto next = [&]() -> const char* { return i + 1 < argc ? argv[++i] : ""; };
        if (arg == "--http-port") {
            config.httpPort = static_cast<uint16_t>(std::atoi(next()));
        } else if (arg == "--http-bind") {
            config.httpBind = next(); // 0.0.0.0 exposes the UI on the LAN
        } else if (arg == "--osc-host") {
            config.oscHost = next();
        } else if (arg == "--osc-port") {
            config.oscPort = static_cast<uint16_t>(std::atoi(next()));
        } else if (arg == "--agents") {
            config.randomAgents = static_cast<uint32_t>(std::atoi(next()));
        } else if (arg == "--seed") {
            config.seed = static_cast<uint32_t>(std::atoi(next()));
        } else if (arg == "--room") {
            float w = 0.0f, h = 0.0f;
            if (std::sscanf(next(), "%fx%f", &w, &h) == 2 && w > 1.0f && h > 1.0f) {
                config.roomSize = {w, h};
            } else {
                std::fprintf(stderr, "invalid --room, expected WxH (e.g. 12x9)\n");
                return 2;
            }
        } else if (arg == "--hokuyo" || arg == "--sick" || arg == "--udp-sensor") {
            // host[:port][@x,y,theta] — for --udp-sensor: port[@x,y,theta]
            std::string spec = next();
            sillage::SensorConfig s;
            s.type = arg == "--hokuyo" ? "hokuyo" : arg == "--sick" ? "sick" : "udp";
            s.port = s.type == "hokuyo" ? 10940 : s.type == "sick" ? 2112 : 9911;
            const size_t at = spec.find('@');
            if (at != std::string::npos) {
                float x = 0.0f, y = 0.0f, theta = 0.0f;
                if (std::sscanf(spec.c_str() + at + 1, "%f,%f,%f", &x, &y, &theta) != 3) {
                    std::fprintf(stderr, "invalid sensor pose, expected @x,y,theta\n");
                    return 2;
                }
                s.pose = {{x, y}, theta};
                spec.resize(at);
            }
            if (s.type == "udp") {
                s.port = static_cast<uint16_t>(std::atoi(spec.c_str()));
            } else {
                const size_t colon = spec.find(':');
                if (colon != std::string::npos) {
                    s.port = static_cast<uint16_t>(std::atoi(spec.c_str() + colon + 1));
                    spec.resize(colon);
                }
                s.host = spec;
            }
            config.sensors.push_back(std::move(s));
        } else if (arg == "--no-sim") {
            config.simEnabled = false;
        } else if (arg == "--ticks") {
            config.maxTicks = static_cast<uint64_t>(std::atoll(next()));
        } else if (arg == "--record") {
            config.recordPath = next();
        } else if (arg == "--replay") {
            config.replayPath = next();
        } else if (arg == "--config") {
            next(); // handled in pass 1
        } else if (arg == "--save-config") {
            std::string error;
            if (!config.toProject().save(next(), error)) {
                std::fprintf(stderr, "save failed: %s\n", error.c_str());
                return 1;
            }
            std::printf("config saved\n");
            return 0;
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
