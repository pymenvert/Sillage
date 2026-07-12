#include "drivers/udpbridge/udp_bridge.h"

#include "core/json.h"

#include <chrono>
#include <vector>

namespace sillage {

void UdpBridgeDriver::start() {
    if (running_.exchange(true)) {
        return;
    }
    thread_ = std::thread([this] { runLoop(); });
}

void UdpBridgeDriver::stop() {
    if (!running_.exchange(false)) {
        return;
    }
    if (thread_.joinable()) {
        thread_.join();
    }
}

void UdpBridgeDriver::runLoop() {
    net::UdpReceiver receiver;
    if (!receiver.bind(config_.bindHost, config_.port, 500)) {
        std::lock_guard lock(mutex_);
        health_.lastError = "cannot bind udp port " + std::to_string(config_.port);
        return;
    }
    {
        std::lock_guard lock(mutex_);
        health_.connected = true; // listening; "connected" = datagrams flowing
        lastRateStamp_ = Clock::now();
    }

    std::vector<char> buffer(1 << 16);
    while (running_) {
        const int n = receiver.receive(buffer.data(), buffer.size());
        if (n <= 0) {
            // Timeout: stale link indicator after 2 s of silence.
            std::lock_guard lock(mutex_);
            if (std::chrono::duration<float>(Clock::now() - lastRateStamp_).count() > 2.0f) {
                health_.connected = false;
                health_.scansPerSecond = 0.0f;
            }
            continue;
        }
        const auto parsed = json::parse(std::string(buffer.data(), static_cast<size_t>(n)));
        if (!parsed.value || !(*parsed.value)["d"].isArray()) {
            std::lock_guard lock(mutex_);
            health_.decodeErrors++;
            continue;
        }
        const json::Value& v = *parsed.value;
        const auto a0 = static_cast<float>(v["a0"].asNumber());
        const auto da = static_cast<float>(v["da"].asNumber(0.008726646f)); // 0.5 deg
        ScanFrame frame;
        frame.sensor = config_.sensorId;
        frame.captureTime = Clock::now();
        const json::Array& d = v["d"].asArray();
        frame.points.reserve(d.size());
        for (size_t i = 0; i < d.size(); ++i) {
            const double mm = d[i].asNumber();
            if (mm <= 0.0 || mm > config_.maxDistanceMm) {
                continue;
            }
            frame.points.push_back(
                {a0 + static_cast<float>(i) * da, static_cast<float>(mm) * 0.001f});
        }
        std::lock_guard lock(mutex_);
        latest_ = std::move(frame);
        ++sequence_;
        health_.connected = true;
        health_.framesReceived++;
        framesSinceStamp_++;
        const auto now = Clock::now();
        const auto elapsed = std::chrono::duration<float>(now - lastRateStamp_).count();
        if (elapsed >= 1.0f) {
            health_.scansPerSecond = static_cast<float>(framesSinceStamp_) / elapsed;
            framesSinceStamp_ = 0;
            lastRateStamp_ = now;
        }
    }
}

std::optional<ScanFrame> UdpBridgeDriver::latestFrame(uint64_t& lastSeenSeq) {
    std::lock_guard lock(mutex_);
    if (sequence_ == lastSeenSeq) {
        return std::nullopt;
    }
    lastSeenSeq = sequence_;
    return latest_;
}

SensorHealth UdpBridgeDriver::health() const {
    std::lock_guard lock(mutex_);
    return health_;
}

} // namespace sillage
