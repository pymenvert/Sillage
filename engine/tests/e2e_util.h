#pragma once

// Shared plumbing for end-to-end tests that drive a live Engine over real
// HTTP and feed it real udp-bridge datagrams. Test-only header.

#include "app/engine.h"
#include "net/net.h"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <memory>
#include <numbers>
#include <string>
#include <thread>
#include <vector>

namespace sillage::e2e {

// Raw HTTP against the live engine, one request per connection (the server
// closes after responding, so recv-to-EOF frames the response).
inline std::string request(uint16_t port, const std::string& method, const std::string& path,
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
inline std::string bodyOf(const std::string& response) {
    const size_t split = response.find("\r\n\r\n");
    return split == std::string::npos ? std::string{} : response.substr(split + 4);
}

// A simulator ScanFrame as a UDP-bridge datagram. Dropped rays become 0 mm
// entries — the bridge's no-return convention — so spacing stays uniform.
inline std::string toDatagram(const ScanFrame& frame, uint32_t rays) {
    const float da = 2.0f * std::numbers::pi_v<float> / static_cast<float>(rays);
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

// A running engine on the first free HTTP port of [firstPort, lastPort).
// port == 0 means none was free. Caller stops and joins.
struct LiveEngine {
    uint16_t port = 0;
    std::unique_ptr<Engine> engine;
    std::thread thread;

    void shutdown() {
        if (engine) {
            engine->stop();
        }
        if (thread.joinable()) {
            thread.join();
        }
    }
};

inline LiveEngine launch(EngineConfig cfg, uint16_t firstPort, uint16_t lastPort) {
    LiveEngine live;
    for (uint16_t p = firstPort; p < lastPort && !live.engine; ++p) {
        cfg.httpPort = p;
        auto candidate = std::make_unique<Engine>(cfg);
        std::thread t([&candidate] { candidate->run(); });
        // Either it binds and serves within a moment, or run() already
        // returned false and the scan moves on.
        bool up = false;
        for (int i = 0; i < 50 && !up; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            up = request(p, "GET", "/api/status").find("200 OK") != std::string::npos;
        }
        if (up) {
            live.port = p;
            live.engine = std::move(candidate);
            live.thread = std::move(t);
        } else {
            candidate->stop();
            t.join();
        }
    }
    return live;
}

} // namespace sillage::e2e
