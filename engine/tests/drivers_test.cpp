#include "drivers/sick/cola.h"
#include "drivers/sick/sick_driver.h"
#include "drivers/udpbridge/udp_bridge.h"
#include "net/net.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <numbers>
#include <thread>

namespace sillage {
namespace {

constexpr float kPi = std::numbers::pi_v<float>;

// --- CoLa A codec ---------------------------------------------------------------

TEST(Cola, TelegramRoundtrip) {
    const std::vector<uint32_t> distances = {1500, 2000, 0, 25000};
    const std::string payload = cola::buildScanTelegram(-kPi * 3.0f / 4.0f, 0.00581f, distances);
    const auto scan = cola::parseScanTelegram(payload);
    ASSERT_TRUE(scan.has_value());
    EXPECT_NEAR(scan->startAngleRad, -kPi * 3.0f / 4.0f, 2e-4f);
    EXPECT_NEAR(scan->angularStepRad, 0.00581f, 2e-4f);
    ASSERT_EQ(scan->distancesMm.size(), distances.size());
    EXPECT_EQ(scan->distancesMm[0], 1500u);
    EXPECT_EQ(scan->distancesMm[3], 25000u);
}

TEST(Cola, RejectsForeignAndTruncatedTelegrams) {
    EXPECT_FALSE(cola::parseScanTelegram("sRA DeviceIdent 8 not scan data").has_value());
    // Truncated: DIST1 announces more values than present.
    std::string cut = cola::buildScanTelegram(0.0f, 0.005f, {100, 200, 300});
    cut.resize(cut.size() - 8);
    EXPECT_FALSE(cola::parseScanTelegram(cut).has_value());
}

// --- SICK driver against a mock CoLa device -----------------------------------------

TEST(SickDriver, DecodesFramesFromMockDevice) {
    net::TcpListener listener;
    uint16_t port = 0;
    for (uint16_t p = 19100; p < 19120; ++p) {
        if (listener.listen("127.0.0.1", p)) {
            port = p;
            break;
        }
    }
    ASSERT_NE(port, 0);

    std::atomic<bool> serving{true};
    std::thread device([&] {
        const net::SocketHandle client = listener.accept();
        if (client == net::kInvalidSocket) {
            return;
        }
        char sink[256];
        net::tcpRecv(client, sink, sizeof(sink)); // consume the subscribe
        // Wall at 3 m across 271 steps of 1 degree, person at 1.2 m in front.
        std::vector<uint32_t> distances(271, 3000);
        for (int i = 130; i <= 140; ++i) {
            distances[static_cast<size_t>(i)] = 1200;
        }
        const std::string telegram =
            cola::frame(cola::buildScanTelegram(-kPi * 3.0f / 4.0f, kPi / 180.0f, distances));
        while (serving) {
            if (!net::tcpSendAll(client, telegram.data(), telegram.size())) {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(40));
        }
        net::tcpClose(client);
    });

    SickDriver::Config config;
    config.host = "127.0.0.1";
    config.port = port;
    config.sensorId = 7;
    SickDriver driver(config);
    driver.start();

    uint64_t seq = 0;
    std::optional<ScanFrame> frame;
    for (int i = 0; i < 200 && !frame; ++i) {
        frame = driver.latestFrame(seq);
        if (!frame) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
    ASSERT_TRUE(frame.has_value()) << "no frame within 2s";
    EXPECT_EQ(frame->sensor, 7u);
    ASSERT_EQ(frame->points.size(), 271u);
    EXPECT_NEAR(frame->points[0].angle, -kPi * 3.0f / 4.0f, 1e-3f);
    EXPECT_NEAR(frame->points[135].distance, 1.2f, 1e-3f);
    EXPECT_NEAR(frame->points[10].distance, 3.0f, 1e-3f);
    EXPECT_TRUE(driver.health().connected);

    driver.stop();
    serving = false;
    listener.close();
    device.join();
}

// --- UDP bridge -------------------------------------------------------------------------

TEST(UdpBridge, DecodesDatagramsAndCountsGarbage) {
    UdpBridgeDriver::Config config;
    config.bindHost = "127.0.0.1";
    config.port = 19230;
    config.sensorId = 4;
    UdpBridgeDriver driver(config);
    driver.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(100)); // bind

    net::UdpSender sender;
    ASSERT_TRUE(sender.open("127.0.0.1", config.port));
    const std::string good = "{\"a0\":-1.5708,\"da\":0.0175,\"d\":[1000,2000,0,3000]}";
    const std::string garbage = "definitely not json";
    sender.send(garbage.data(), garbage.size());
    sender.send(good.data(), good.size());

    uint64_t seq = 0;
    std::optional<ScanFrame> frame;
    for (int i = 0; i < 200 && !frame; ++i) {
        frame = driver.latestFrame(seq);
        if (!frame) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            sender.send(good.data(), good.size()); // datagrams may race the bind
        }
    }
    ASSERT_TRUE(frame.has_value());
    EXPECT_EQ(frame->sensor, 4u);
    ASSERT_EQ(frame->points.size(), 3u); // the 0 mm no-return is dropped
    EXPECT_NEAR(frame->points[0].angle, -1.5708f, 1e-4f);
    EXPECT_NEAR(frame->points[0].distance, 1.0f, 1e-4f);
    EXPECT_NEAR(frame->points[2].angle, -1.5708f + 3 * 0.0175f, 1e-4f);
    EXPECT_GE(driver.health().decodeErrors, 1u);
    driver.stop();
}

} // namespace
} // namespace sillage
