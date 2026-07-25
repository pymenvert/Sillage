#pragma once

#include "core/types.h"
#include "drivers/driver.h"
#include "drivers/hokuyo/scip.h"
#include "net/net.h"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace sillage {

// Hokuyo URG/UST driver over Ethernet (SCIP 2.2, TCP port 10940).
// Runs its own thread: connect -> BM -> MD stream -> decode -> latest frame.
// Reconnects with backoff on any failure. The pipeline polls latestFrame().
//
// Verified against the in-process mock SCIP server (tests); validation against
// physical hardware is pending first sensor delivery — protocol details that
// only real devices exhibit are tracked in docs/07 (M1).
class HokuyoDriver : public ISensorDriver {
public:
    struct Config {
        std::string host;
        uint16_t port = 10940;
        SensorId sensorId = 0;
        scip::Geometry geometry{};
        int minDistanceMm = 20;    // below: error codes / dust
        int maxDistanceMm = 30000;
    };

    explicit HokuyoDriver(Config config) : config_(std::move(config)) {}
    ~HokuyoDriver() override { stop(); }
    HokuyoDriver(const HokuyoDriver&) = delete;
    HokuyoDriver& operator=(const HokuyoDriver&) = delete;

    const char* type() const override { return "hokuyo"; }
    void start() override;
    void stop() override;
    std::optional<ScanFrame> latestFrame(uint64_t& lastSeenSeq) override;
    SensorHealth health() const override;

private:
    void runLoop();
    bool session(); // one connect->stream session; false = retry after backoff

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
