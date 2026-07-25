#include "drivers/sick/sick_driver.h"

#include <chrono>
#include <cstdint>

namespace sillage {

void SickDriver::start() {
    if (running_.exchange(true)) {
        return;
    }
    thread_ = std::thread([this] { runLoop(); });
}

void SickDriver::stop() {
    if (!running_.exchange(false)) {
        return;
    }
    if (thread_.joinable()) {
        thread_.join();
    }
}

void SickDriver::runLoop() {
    while (running_) {
        if (!session() && running_) {
            {
                std::lock_guard lock(mutex_);
                health_.connected = false;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        }
    }
}

bool SickDriver::session() {
    const net::SocketHandle socket = net::tcpConnect(config_.host, config_.port, 2000);
    if (socket == net::kInvalidSocket) {
        std::lock_guard lock(mutex_);
        health_.lastError = "connect failed";
        return false;
    }
    const auto fail = [&](const char* what) {
        net::tcpClose(socket);
        std::lock_guard lock(mutex_);
        health_.connected = false;
        health_.lastError = what;
        return false;
    };

    const std::string subscribe = cola::subscribeScans();
    if (!net::tcpSendAll(socket, subscribe.data(), subscribe.size())) {
        return fail("subscribe failed");
    }
    {
        std::lock_guard lock(mutex_);
        health_.connected = true;
        health_.lastError.clear();
        lastRateStamp_ = Clock::now();
        framesSinceStamp_ = 0;
    }

    // STX...ETX telegram framing.
    std::string buffer;
    char chunk[4096];
    while (running_) {
        const int n = net::tcpRecv(socket, chunk, sizeof(chunk));
        if (n <= 0) {
            return fail("stream lost");
        }
        buffer.append(chunk, static_cast<size_t>(n));
        if (buffer.size() > (1 << 20)) {
            return fail("protocol garbage");
        }

        size_t stx;
        size_t etx;
        while ((stx = buffer.find(cola::kStx)) != std::string::npos &&
               (etx = buffer.find(cola::kEtx, stx)) != std::string::npos) {
            const std::string payload = buffer.substr(stx + 1, etx - stx - 1);
            buffer.erase(0, etx + 1);

            const auto scan = cola::parseScanTelegram(payload);
            if (!scan) {
                if (payload.rfind("sSN LMDscandata", 0) == 0) {
                    std::lock_guard lock(mutex_);
                    health_.decodeErrors++;
                }
                continue; // acknowledges and other telegrams are fine to skip
            }
            ScanFrame frame;
            frame.sensor = config_.sensorId;
            frame.captureTime = Clock::now();
            frame.points.reserve(scan->distancesMm.size());
            for (size_t i = 0; i < scan->distancesMm.size(); ++i) {
                const auto mm = static_cast<int>(scan->distancesMm[i]);
                if (mm < config_.minDistanceMm || mm > config_.maxDistanceMm) {
                    continue;
                }
                frame.points.push_back(
                    {scan->startAngleRad + static_cast<float>(i) * scan->angularStepRad,
                     static_cast<float>(mm) * 0.001f});
            }
            std::lock_guard lock(mutex_);
            latest_ = std::move(frame);
            ++sequence_;
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
    net::tcpClose(socket);
    return true;
}

std::optional<ScanFrame> SickDriver::latestFrame(uint64_t& lastSeenSeq) {
    std::lock_guard lock(mutex_);
    if (sequence_ == lastSeenSeq) {
        return std::nullopt;
    }
    lastSeenSeq = sequence_;
    return latest_;
}

SensorHealth SickDriver::health() const {
    std::lock_guard lock(mutex_);
    return health_;
}

} // namespace sillage
