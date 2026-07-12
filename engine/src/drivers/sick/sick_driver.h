#pragma once

#include "drivers/driver.h"
#include "drivers/sick/cola.h"
#include "net/net.h"

#include <atomic>
#include <mutex>
#include <thread>

namespace sillage {

// SICK TiM 5xx/7xx driver over Ethernet (CoLa A, TCP port 2112): subscribe to
// LMDscandata pushes, decode DIST1, reconnect with backoff.
// Verified against the in-process mock device (tests); physical-hardware
// validation pending (docs/07).
class SickDriver : public ISensorDriver {
public:
    struct Config {
        std::string host;
        uint16_t port = 2112;
        SensorId sensorId = 0;
        int minDistanceMm = 20;
        int maxDistanceMm = 30000;
    };

    explicit SickDriver(Config config) : config_(std::move(config)) {}
    ~SickDriver() override { stop(); }
    SickDriver(const SickDriver&) = delete;
    SickDriver& operator=(const SickDriver&) = delete;

    const char* type() const override { return "sick"; }
    void start() override;
    void stop() override;
    std::optional<ScanFrame> latestFrame(uint64_t& lastSeenSeq) override;
    SensorHealth health() const override;

private:
    void runLoop();
    bool session();

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
