#pragma once

#include "net/net.h"

#include <atomic>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace sillage::net {

// Computes the Sec-WebSocket-Accept value for a handshake key (RFC 6455).
// Exposed for tests.
std::string websocketAcceptKey(const std::string& clientKey);

// Minimal HTTP + WebSocket server for the M0 dev UI: serves static files from
// a doc root and pushes text frames to every connected WebSocket client.
// Push-only by design; client frames are consumed (ping/close honored).
// Replaced by a full API server (uWebSockets) when the REST config API lands.
class WsHttpServer {
public:
    ~WsHttpServer();

    bool start(const std::string& bindHost, uint16_t port, std::filesystem::path docRoot);
    void stop();

    // Handler for GET /api/* — returns a JSON body, or empty string for 404.
    // Must be set before start(); called from the accept thread.
    void setApiHandler(std::function<std::string(const std::string&)> handler) {
        apiHandler_ = std::move(handler);
    }

    // Sends a text frame to all connected WebSocket clients. Clients that
    // cannot keep up (send timeout) are dropped — a slow tab must never be
    // able to stall the pipeline thread.
    void broadcast(const std::string& text);

    size_t clientCount() const;

private:
    void acceptLoop();
    void handleConnection(SocketHandle client);
    void serveHttp(SocketHandle client, const std::string& target);

    TcpListener listener_;
    std::filesystem::path docRoot_;
    std::function<std::string(const std::string&)> apiHandler_;
    std::thread acceptThread_;
    std::atomic<bool> running_{false};

    mutable std::mutex clientsMutex_;
    std::vector<SocketHandle> clients_;
    std::vector<std::thread> readerThreads_;
};

} // namespace sillage::net
