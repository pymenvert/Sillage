#include "drivers/hokuyo/hokuyo_driver.h"

#include <chrono>

namespace sillage {

namespace {

// Reads LF-terminated lines from a socket into a small buffered reader.
class LineReader {
public:
    explicit LineReader(net::SocketHandle socket) : socket_(socket) {}

    // Blocking (bounded by the socket recv timeout). nullopt = socket error.
    std::optional<std::string> readLine() {
        while (true) {
            const size_t lf = buffer_.find('\n');
            if (lf != std::string::npos) {
                std::string line = buffer_.substr(0, lf);
                buffer_.erase(0, lf + 1);
                return line;
            }
            char chunk[2048];
            const int n = net::tcpRecv(socket_, chunk, sizeof(chunk));
            if (n <= 0) {
                return std::nullopt;
            }
            buffer_.append(chunk, static_cast<size_t>(n));
            if (buffer_.size() > 1 << 20) {
                return std::nullopt; // protocol garbage; bail out
            }
        }
    }

private:
    net::SocketHandle socket_;
    std::string buffer_;
};

} // namespace

void HokuyoDriver::start() {
    if (running_.exchange(true)) {
        return;
    }
    thread_ = std::thread([this] { runLoop(); });
}

void HokuyoDriver::stop() {
    if (!running_.exchange(false)) {
        return;
    }
    if (thread_.joinable()) {
        thread_.join();
    }
}

void HokuyoDriver::runLoop() {
    while (running_) {
        if (!session() && running_) {
            {
                std::lock_guard lock(mutex_);
                health_.connected = false;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1000)); // backoff
        }
    }
}

bool HokuyoDriver::session() {
    const net::SocketHandle socket = net::tcpConnect(config_.host, config_.port, 2000);
    if (socket == net::kInvalidSocket) {
        std::lock_guard lock(mutex_);
        health_.lastError = "connect failed";
        return false;
    }

    LineReader reader(socket);
    const auto fail = [&](const char* what) {
        net::tcpClose(socket);
        std::lock_guard lock(mutex_);
        health_.connected = false;
        health_.lastError = what;
        return false;
    };

    // Laser on, then infinite MD stream.
    const std::string bm = "BM\n";
    if (!net::tcpSendAll(socket, bm.data(), bm.size())) {
        return fail("send BM failed");
    }
    const std::string md = scip::buildMD(config_.geometry.startStep, config_.geometry.endStep,
                                         /*cluster=*/0, /*interval=*/0, /*count=*/0);
    if (!net::tcpSendAll(socket, md.data(), md.size())) {
        return fail("send MD failed");
    }
    {
        std::lock_guard lock(mutex_);
        health_.connected = true;
        health_.lastError.clear();
        lastRateStamp_ = Clock::now();
        framesSinceStamp_ = 0;
    }

    // Read blocks: echo line, payload lines, empty line.
    std::vector<std::string> block;
    while (running_) {
        const auto line = reader.readLine();
        if (!line) {
            return fail("stream lost");
        }
        if (!line->empty()) {
            block.push_back(*line);
            continue;
        }

        // Block complete. block[0] is the echo; hand the rest to the codec.
        if (block.size() >= 2 && block[0].rfind("MD", 0) == 0) {
            const std::vector<std::string> payload(block.begin() + 1, block.end());
            const auto frame = scip::parseMDBlock(payload);
            if (frame) {
                ScanFrame scan;
                scan.sensor = config_.sensorId;
                scan.captureTime = Clock::now(); // TODO(M2): sensor timestamp fusion
                scan.points.reserve(frame->distancesMm.size());
                for (size_t i = 0; i < frame->distancesMm.size(); ++i) {
                    const auto mm = static_cast<int>(frame->distancesMm[i]);
                    if (mm < config_.minDistanceMm || mm > config_.maxDistanceMm) {
                        continue;
                    }
                    const int step = config_.geometry.startStep + static_cast<int>(i);
                    scan.points.push_back({config_.geometry.angleOfStep(step),
                                           static_cast<float>(mm) * 0.001f});
                }
                std::lock_guard lock(mutex_);
                latest_ = std::move(scan);
                ++sequence_;
                health_.framesReceived++;
                framesSinceStamp_++;
                const auto now = Clock::now();
                const auto elapsed =
                    std::chrono::duration<float>(now - lastRateStamp_).count();
                if (elapsed >= 1.0f) {
                    health_.scansPerSecond = static_cast<float>(framesSinceStamp_) / elapsed;
                    framesSinceStamp_ = 0;
                    lastRateStamp_ = now;
                }
            } else {
                std::lock_guard lock(mutex_);
                // Acknowledge blocks ("00P") are expected once; count real
                // decode failures only for status "99" blocks.
                if (!payload.empty() && payload[0].rfind("99", 0) == 0) {
                    health_.decodeErrors++;
                }
            }
        }
        block.clear();
    }
    net::tcpClose(socket);
    return true; // clean shutdown
}

std::optional<ScanFrame> HokuyoDriver::latestFrame(uint64_t& lastSeenSeq) {
    std::lock_guard lock(mutex_);
    if (sequence_ == lastSeenSeq) {
        return std::nullopt;
    }
    lastSeenSeq = sequence_;
    return latest_;
}

SensorHealth HokuyoDriver::health() const {
    std::lock_guard lock(mutex_);
    return health_;
}

} // namespace sillage
