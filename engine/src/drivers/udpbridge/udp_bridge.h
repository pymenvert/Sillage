#pragma once

#include "drivers/driver.h"
#include "net/net.h"

#include <atomic>
#include <mutex>
#include <thread>

namespace sillage {

// Universal UDP bridge: ANY sensor becomes a Sillage source through a tiny
// script that pushes JSON datagrams — RPLIDAR via the Python rplidar lib,
// a 3D lidar sliced by its own SDK, even a phone. See tools/bridges/.
//
// Datagram (one full revolution per datagram, UDP up to 64 KB):
//   {"a0": <start angle rad>, "da": <angle step rad>, "d": [<mm>, <mm>, ...]}
// Entries <= 0 mm are dropped (no-return).
class UdpBridgeDriver : public ISensorDriver {
public:
    struct Config {
        std::string bindHost = "0.0.0.0";
        uint16_t port = 9911;
        SensorId sensorId = 0;
        int maxDistanceMm = 60000;
    };

    explicit UdpBridgeDriver(Config config) : config_(std::move(config)) {}
    ~UdpBridgeDriver() override { stop(); }
    UdpBridgeDriver(const UdpBridgeDriver&) = delete;
    UdpBridgeDriver& operator=(const UdpBridgeDriver&) = delete;

    const char* type() const override { return "udp"; }
    void start() override;
    void stop() override;
    std::optional<ScanFrame> latestFrame(uint64_t& lastSeenSeq) override;
    SensorHealth health() const override;

private:
    void runLoop();

    Config config_;
    std::thread thread_;
    std::atomic<bool> running_{false};

    mutable std::mutex mutex_;
    ScanFrame latest_;
    uint64_t sequence_ = 0;
    SensorHealth health_;
    TimePoint lastRateStamp_{};
    uint32_t framesSinceStamp_ = 0;
};

} // namespace sillage
