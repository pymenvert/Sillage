#include "app/engine.h"
#include "core/json.h"
#include "net/net.h"
#include "sim/simulator.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <numbers>
#include <string>
#include <thread>
#include <vector>

namespace sillage {
namespace {

constexpr float kPi = std::numbers::pi_v<float>;

// Raw HTTP against the live engine, one request per connection (the server
// closes after responding, so recv-to-EOF frames the response).
std::string request(uint16_t port, const std::string& method, const std::string& path,
                    const std::string& body = {}) {
    const net::SocketHandle s = net::tcpConnect("127.0.0.1", port, 2000);
    if (s == net::kInvalidSocket) {
        return {};
    }
    std::string raw = method + " " + path + " HTTP/1.1\r\nHost: x\r\n";
    if (!body.empty()) {
        raw += "Content-Length: " + std::to_string(body.size()) + "\r\n";
    }
    raw += "\r\n" + body;
    net::tcpSendAll(s, raw.data(), raw.size());
    std::string response;
    char buf[4096];
    int n;
    while ((n = net::tcpRecv(s, buf, sizeof(buf))) > 0) {
        response.append(buf, static_cast<size_t>(n));
    }
    net::tcpClose(s);
    return response;
}

// Body of an HTTP response (everything after the blank line).
std::string bodyOf(const std::string& response) {
    const size_t split = response.find("\r\n\r\n");
    return split == std::string::npos ? std::string{} : response.substr(split + 4);
}

// A simulator ScanFrame as a UDP-bridge datagram. Dropped rays become 0 mm
// entries — the bridge's no-return convention — so spacing stays uniform.
std::string toDatagram(const ScanFrame& frame, uint32_t rays) {
    const float da = 2.0f * kPi / static_cast<float>(rays);
    std::vector<int> mm(rays, 0);
    for (const RangePoint& p : frame.points) {
        const auto idx = static_cast<size_t>(std::lround(p.angle / da)) % rays;
        mm[idx] = static_cast<int>(p.distance * 1000.0f);
    }
    std::string out = "{\"a0\":0,\"da\":" + std::to_string(da) + ",\"d\":[";
    for (size_t i = 0; i < mm.size(); ++i) {
        out += (i ? "," : "") + std::to_string(mm[i]);
    }
    return out + "]}";
}

// The full product path of the calibration workflow, end to end: two REAL
// udp-bridge drivers fed by a one-walker simulator scanning from the TRUE
// poses, an engine configured with sensor 1 HALF A METER AND 11 DEGREES OFF,
// and the /api/calib routes driven exactly as the future UI will drive them —
// start, watch observations grow, solve, apply. The solver must recover the
// true pose over HTTP, and apply must hot-apply it into /api/config without
// a restart.
TEST(CalibApi, RecoversWrongPoseOverHttpEndToEnd) {
    const SensorPose truth0{{0.15f, 0.15f}, 0.0f};
    const SensorPose truth1{{9.85f, 7.85f}, kPi};
    const SensorPose wrong1{{9.45f, 7.55f}, kPi + 0.2f};
    constexpr uint16_t kUdpPort0 = 19341, kUdpPort1 = 19342;

    EngineConfig cfg;
    cfg.simEnabled = false;
    cfg.roomSize = {10.0f, 8.0f};
    cfg.sensors = {{"udp", "127.0.0.1", kUdpPort0, truth0},
                   {"udp", "127.0.0.1", kUdpPort1, wrong1}};
    cfg.httpBind = "127.0.0.1";
    cfg.oscEnabled = false;
    cfg.tickHz = 240.0f; // 4x wall-clock: the walk fits in a few seconds
    cfg.uiRoot = std::filesystem::temp_directory_path() / "sillage_calib_api_ui";
    std::filesystem::create_directories(cfg.uiRoot);

    // The engine owns port selection failures: scan a small range.
    uint16_t httpPort = 0;
    std::unique_ptr<Engine> engine;
    std::thread engineThread;
    for (uint16_t p = 19350; p < 19370 && !engine; ++p) {
        cfg.httpPort = p;
        auto candidate = std::make_unique<Engine>(cfg);
        std::thread t([&candidate] { candidate->run(); });
        // The engine either binds and serves within a moment, or run()
        // returned false and the port scan continues.
        bool up = false;
        for (int i = 0; i < 50 && !up; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            up = request(p, "GET", "/api/status").find("200 OK") != std::string::npos;
        }
        if (up) {
            httpPort = p;
            engine = std::move(candidate);
            engineThread = std::move(t);
        } else {
            candidate->stop();
            t.join();
        }
    }
    ASSERT_NE(httpPort, 0) << "no free HTTP port for the engine";

    // One walker ping-ponging the diagonal, scanned from the TRUE poses.
    Simulator::Params sp;
    sp.roomSize = cfg.roomSize;
    sp.agents = {{{1.0f, 1.0f}, {9.0f, 7.0f}, 1.1f, Simulator::Motion::PingPong, 1.5f}};
    Simulator sim(sp);
    sim.addSensor(truth0);
    sim.addSensor(truth1);

    std::atomic<bool> feeding{true};
    std::thread feeder([&] {
        // No gtest asserts here: fatal assertions only work on the main
        // thread. A failed open leaves the counters flat, which the
        // observation deadline below reports with context.
        net::UdpSender s0, s1;
        if (!s0.open("127.0.0.1", kUdpPort0) || !s1.open("127.0.0.1", kUdpPort1)) {
            return;
        }
        while (feeding.load()) {
            const auto frames = sim.step(1.0f / 60.0f, TimePoint{});
            for (const ScanFrame& f : frames) {
                const std::string datagram = toDatagram(f, sp.raysPerScan);
                (f.sensor == 0 ? s0 : s1).send(datagram.data(), datagram.size());
            }
            // 4x real time, matching the 240 Hz tick.
            std::this_thread::sleep_for(std::chrono::milliseconds(4));
        }
    });

    EXPECT_NE(bodyOf(request(httpPort, "POST", "/api/calib/start")).find("\"collecting\":true"),
              std::string::npos);

    // Wait for both sensors to accumulate correspondences (background learn
    // first, then the walker becomes foreground).
    bool enough = false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (!enough && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
        const auto parsed = json::parse(bodyOf(request(httpPort, "GET", "/api/calib/status")));
        if (!parsed.value) {
            continue;
        }
        const auto& obs = (*parsed.value)["observations"].asArray();
        enough = obs.size() == 2 && obs[0].asNumber() >= 150 && obs[1].asNumber() >= 150;
    }
    ASSERT_TRUE(enough) << "observations never accumulated: "
                        << bodyOf(request(httpPort, "GET", "/api/calib/status"));

    const auto solved =
        json::parse(bodyOf(request(httpPort, "POST", "/api/calib/solve", "{\"anchor\":0}")));
    ASSERT_TRUE(solved.value.has_value());
    const auto& results = (*solved.value)["results"].asArray();
    ASSERT_EQ(results.size(), 2u);
    ASSERT_TRUE(results[1]["solved"].asBool()) << results[1]["message"].asString();
    const auto rx = static_cast<float>(results[1]["x"].asNumber());
    const auto ry = static_cast<float>(results[1]["y"].asNumber());
    auto rtheta = static_cast<float>(results[1]["theta"].asNumber());
    const float posError = std::hypot(rx - truth1.position.x, ry - truth1.position.y);
    float angleError = rtheta - truth1.theta;
    while (angleError > kPi) { angleError -= 2.0f * kPi; }
    while (angleError < -kPi) { angleError += 2.0f * kPi; }
    EXPECT_LT(posError, 0.06f) << "recovered (" << rx << "," << ry << ")";
    EXPECT_LT(std::abs(angleError), 0.03f);

    // Apply writes the solved poses through the ordinary config path and they
    // must show up on /api/config without a restart. Both sensors count as
    // applied: the anchor's result echoes its own pinned pose (solved=true by
    // contract), so writing it back is an idempotent no-op.
    const std::string applied = bodyOf(request(httpPort, "POST", "/api/calib/apply"));
    EXPECT_NE(applied.find("\"applied\":2"), std::string::npos) << applied;
    std::this_thread::sleep_for(std::chrono::milliseconds(100)); // next tick applies
    const auto config = json::parse(bodyOf(request(httpPort, "GET", "/api/config")));
    ASSERT_TRUE(config.value.has_value());
    const auto& sensors = (*config.value)["sensors"].asArray();
    ASSERT_EQ(sensors.size(), 2u);
    EXPECT_NEAR(static_cast<float>(sensors[1]["theta"].asNumber()), rtheta, 1e-4f);

    feeding = false;
    feeder.join();
    engine->stop();
    engineThread.join();
    std::error_code ec;
    std::filesystem::remove_all(cfg.uiRoot, ec);
}

} // namespace
} // namespace sillage
