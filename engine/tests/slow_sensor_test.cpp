#include "core/json.h"
#include "e2e_util.h"
#include "sim/simulator.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <thread>

namespace sillage {
namespace {

// A sensor slower than the tick is the NORMAL case, not a corner: a SICK TiM
// revolves at 15 Hz against the 60 Hz tick. Without holding each sensor's
// last scan between revolutions, that sensor yields detections with 3-tick
// gaps — tentativeMaxMiss (2) kills every probationary track before its
// confirmHits-th (5) hit, and a room covered by slow sensors alone tracks
// NOBODY, ever. This drives the real product path: a real udp-bridge driver
// fed at 15 Hz, a walker crossing the room, and /api/status must report a
// track, sustained.
TEST(SlowSensor, A15HzSensorStillProducesSustainedTracks) {
    const SensorPose pose{{0.15f, 0.15f}, 0.0f};
    constexpr uint16_t kUdpPort = 19541;

    EngineConfig cfg;
    cfg.simEnabled = false;
    cfg.roomSize = {10.0f, 8.0f};
    cfg.sensors = {{"udp", "127.0.0.1", kUdpPort, pose}};
    cfg.httpBind = "127.0.0.1";
    cfg.oscEnabled = false;
    cfg.tickHz = 120.0f; // sensor at 15 Hz: one revolution every 8 ticks
    cfg.uiRoot = std::filesystem::temp_directory_path() / "sillage_slow_sensor_ui";
    std::filesystem::create_directories(cfg.uiRoot);

    auto live = e2e::launch(cfg, 19560, 19580);
    ASSERT_NE(live.port, 0) << "no free HTTP port for the engine";

    // One walker, scanned from the true pose, delivered at 15 Hz wall clock.
    Simulator::Params sp;
    sp.roomSize = cfg.roomSize;
    sp.agents = {{{1.0f, 1.0f}, {9.0f, 7.0f}, 1.1f, Simulator::Motion::PingPong, 1.5f}};
    Simulator sim(sp);
    sim.addSensor(pose);

    std::atomic<bool> feeding{true};
    std::thread feeder([&] {
        net::UdpSender sender;
        if (!sender.open("127.0.0.1", kUdpPort)) {
            return; // the deadline below reports it with context
        }
        while (feeding.load()) {
            const auto frames = sim.step(1.0f / 15.0f, TimePoint{});
            const std::string datagram = e2e::toDatagram(frames[0], sp.raysPerScan);
            sender.send(datagram.data(), datagram.size());
            std::this_thread::sleep_for(std::chrono::milliseconds(66)); // 15 Hz
        }
    });

    // A track must appear and STAY: three consecutive polls, so a flickering
    // tentative that dies between revolutions cannot pass.
    int consecutive = 0;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (consecutive < 3 && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        const auto parsed =
            json::parse(e2e::bodyOf(e2e::request(live.port, "GET", "/api/status")));
        const bool tracked = parsed.value && (*parsed.value)["tracks"].asNumber() >= 1.0;
        consecutive = tracked ? consecutive + 1 : 0;
    }
    EXPECT_EQ(consecutive, 3)
        << "no sustained track from a 15 Hz sensor: "
        << e2e::bodyOf(e2e::request(live.port, "GET", "/api/status"));

    feeding = false;
    feeder.join();
    live.shutdown();
    std::error_code ec;
    std::filesystem::remove_all(cfg.uiRoot, ec);
}

} // namespace
} // namespace sillage
