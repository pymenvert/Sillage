#include "drivers/hokuyo/hokuyo_driver.h"
#include "drivers/hokuyo/scip.h"
#include "net/net.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <thread>

namespace sillage {
namespace {

// --- Codec unit tests ---------------------------------------------------------

TEST(Scip, EncodeDecodeRoundtrip) {
    for (const uint32_t v : {0u, 1u, 63u, 64u, 4095u, 123456u, 262143u}) {
        const std::string enc = scip::encodeValue(v, 3);
        ASSERT_EQ(enc.size(), 3u);
        const auto dec = scip::decodeValue(enc.c_str(), 3);
        ASSERT_TRUE(dec.has_value());
        EXPECT_EQ(*dec, v & 0x3FFFF);
    }
}

TEST(Scip, ChecksumMatchesKnownStatusLine) {
    // The well-known streaming status line is "99b".
    EXPECT_EQ(scip::checksumChar("99"), 'b');
    const auto stripped = scip::stripChecksum("99b");
    ASSERT_TRUE(stripped.has_value());
    EXPECT_EQ(*stripped, "99");
    EXPECT_FALSE(scip::stripChecksum("99x").has_value());
}

TEST(Scip, BuildMDFormat) {
    EXPECT_EQ(scip::buildMD(0, 1080, 0, 0, 0), "MD0000108000000\n");
}

std::vector<std::string> makeDataBlock(uint32_t timestamp,
                                       const std::vector<uint32_t>& distancesMm) {
    std::vector<std::string> lines;
    lines.push_back(std::string("99") + scip::checksumChar("99"));
    const std::string ts = scip::encodeValue(timestamp, 4);
    lines.push_back(ts + scip::checksumChar(ts));
    std::string data;
    for (const uint32_t d : distancesMm) {
        data += scip::encodeValue(d, 3);
    }
    for (size_t i = 0; i < data.size(); i += 64) {
        const std::string chunk = data.substr(i, 64);
        lines.push_back(chunk + scip::checksumChar(chunk));
    }
    return lines;
}

TEST(Scip, ParseMDBlockRoundtrip) {
    const std::vector<uint32_t> distances = {1000, 2000, 30000, 0, 65535};
    const auto frame = scip::parseMDBlock(makeDataBlock(123456, distances));
    ASSERT_TRUE(frame.has_value());
    EXPECT_EQ(frame->timestampMs, 123456u);
    ASSERT_EQ(frame->distancesMm.size(), distances.size());
    EXPECT_EQ(frame->distancesMm[0], 1000u);
    EXPECT_EQ(frame->distancesMm[2], 30000u);
}

TEST(Scip, ParseMDBlockRejectsCorruptChecksum) {
    auto block = makeDataBlock(1, {1000, 2000});
    block.back()[0] ^= 1; // corrupt one data byte
    EXPECT_FALSE(scip::parseMDBlock(block).has_value());
}

TEST(Scip, ParseMDBlockRejectsAcknowledge) {
    // The immediate "00P" acknowledge block must not decode as data.
    const std::vector<std::string> ack = {std::string("00") + scip::checksumChar("00"), "", ""};
    EXPECT_FALSE(scip::parseMDBlock(ack).has_value());
}

// The default geometry describes a UST-10LX/20LX: 1081 steps spanning exactly
// 270°, front at step 540. A resolution that disagrees with the step range
// puts every point at the wrong bearing, and once the span passes 360° the
// polar background bins alias onto the opposite bearing — people standing near
// the room edges get subtracted away as background.
TEST(Scip, DefaultGeometrySpans270Degrees) {
    const scip::Geometry g;
    constexpr float kPi = 3.14159265358979f;

    EXPECT_NEAR(g.angleOfStep(g.frontStep), 0.0f, 1e-6f);
    EXPECT_NEAR(g.angleOfStep(g.startStep), -0.75f * kPi, 1e-4f);
    EXPECT_NEAR(g.angleOfStep(g.endStep), 0.75f * kPi, 1e-4f);

    const float span = g.angleOfStep(g.endStep) - g.angleOfStep(g.startStep);
    EXPECT_NEAR(span, 1.5f * kPi, 1e-4f);
    EXPECT_LT(span, 2.0f * kPi) << "a span past a full turn aliases in the background bins";
}

// --- Driver integration against an in-process mock sensor ---------------------

// Minimal SCIP 2.2 device: accepts one client, acknowledges BM/MD, then
// streams data blocks at ~40 Hz until stopped.
class MockScipServer {
public:
    explicit MockScipServer(std::vector<uint32_t> distances)
        : distances_(std::move(distances)) {}

    ~MockScipServer() { stop(); }

    uint16_t start() {
        // Ephemeral-ish port: try a small range to dodge collisions.
        for (uint16_t port = 18940; port < 18960; ++port) {
            if (listener_.listen("127.0.0.1", port)) {
                port_ = port;
                break;
            }
        }
        EXPECT_NE(port_, 0) << "no free mock port";
        running_ = true;
        thread_ = std::thread([this] { serve(); });
        return port_;
    }

    void stop() {
        if (!running_.exchange(false)) {
            return;
        }
        listener_.close();
        if (thread_.joinable()) {
            thread_.join();
        }
    }

private:
    void serve() {
        const net::SocketHandle client = listener_.accept();
        if (client == net::kInvalidSocket) {
            return;
        }
        std::string rx;
        char buf[512];
        std::string mdEcho;
        // Consume BM and MD commands (echo + status ack for each).
        while (running_ && mdEcho.empty()) {
            const int n = net::tcpRecv(client, buf, sizeof(buf));
            if (n <= 0) {
                net::tcpClose(client);
                return;
            }
            rx.append(buf, static_cast<size_t>(n));
            size_t lf;
            while ((lf = rx.find('\n')) != std::string::npos) {
                const std::string cmd = rx.substr(0, lf);
                rx.erase(0, lf + 1);
                if (cmd.rfind("BM", 0) == 0) {
                    const std::string resp =
                        cmd + "\n" + "00" + scip::checksumChar("00") + "\n\n";
                    net::tcpSendAll(client, resp.data(), resp.size());
                } else if (cmd.rfind("MD", 0) == 0) {
                    mdEcho = cmd;
                    const std::string resp =
                        cmd + "\n" + "00" + scip::checksumChar("00") + "\n\n";
                    net::tcpSendAll(client, resp.data(), resp.size());
                }
            }
        }
        // Stream data blocks.
        uint32_t ts = 0;
        while (running_) {
            std::string block = mdEcho + "\n";
            for (const std::string& line : makeDataBlock(ts, distances_)) {
                block += line + "\n";
            }
            block += "\n";
            if (!net::tcpSendAll(client, block.data(), block.size())) {
                break;
            }
            ts += 25;
            std::this_thread::sleep_for(std::chrono::milliseconds(25));
        }
        net::tcpClose(client);
    }

    std::vector<uint32_t> distances_;
    net::TcpListener listener_;
    std::thread thread_;
    std::atomic<bool> running_{false};
    uint16_t port_ = 0;
};

TEST(HokuyoDriver, ReceivesAndDecodesFramesFromMockSensor) {
    // Flat wall at 2 m, one "person" at 1 m around the front step.
    std::vector<uint32_t> distances(1081, 2000);
    for (int step = 520; step <= 560; ++step) {
        distances[static_cast<size_t>(step)] = 1000;
    }
    MockScipServer server(distances);
    const uint16_t port = server.start();

    HokuyoDriver::Config config;
    config.host = "127.0.0.1";
    config.port = port;
    config.sensorId = 3;
    HokuyoDriver driver(config);
    driver.start();

    // Wait for a couple of frames (bounded).
    uint64_t seq = 0;
    std::optional<ScanFrame> frame;
    for (int i = 0; i < 200 && !frame; ++i) {
        frame = driver.latestFrame(seq);
        if (!frame) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
    ASSERT_TRUE(frame.has_value()) << "no frame within 2s";
    EXPECT_EQ(frame->sensor, 3u);
    ASSERT_EQ(frame->points.size(), 1081u);

    // Front step (540) is at angle ~0 and inside the "person".
    const RangePoint front = frame->points[540];
    EXPECT_NEAR(front.angle, 0.0f, 1e-4f);
    EXPECT_NEAR(front.distance, 1.0f, 1e-3f);
    // A step on the wall, at its true bearing: step 100 is 440 steps before
    // the front, i.e. -110° on a 0.25°/step sensor.
    EXPECT_NEAR(frame->points[100].distance, 2.0f, 1e-3f);
    EXPECT_NEAR(frame->points[100].angle, -110.0f * 3.14159265358979f / 180.0f, 1e-4f);

    const SensorHealth health = driver.health();
    EXPECT_TRUE(health.connected);
    EXPECT_GE(health.framesReceived, 1u);
    EXPECT_EQ(health.decodeErrors, 0u);

    driver.stop();
    server.stop();
}

TEST(HokuyoDriver, SurvivesServerDisappearing) {
    std::vector<uint32_t> distances(1081, 1500);
    auto server = std::make_unique<MockScipServer>(distances);
    const uint16_t port = server->start();

    HokuyoDriver::Config config;
    config.host = "127.0.0.1";
    config.port = port;
    HokuyoDriver driver(config);
    driver.start();

    uint64_t seq = 0;
    std::optional<ScanFrame> frame;
    for (int i = 0; i < 200 && !frame; ++i) {
        frame = driver.latestFrame(seq);
        if (!frame) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
    ASSERT_TRUE(frame.has_value());

    server->stop();
    server.reset(); // sensor vanishes
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    EXPECT_FALSE(driver.health().connected); // reported, not silently stuck
    driver.stop();                            // clean shutdown while reconnecting
}

} // namespace
} // namespace sillage
