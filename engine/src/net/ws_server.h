#pragma once

#include "net/net.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace sillage::net {

// Computes the Sec-WebSocket-Accept value for a handshake key (RFC 6455).
// Exposed for tests.
std::string websocketAcceptKey(const std::string& clientKey);

// HTTP + WebSocket server: serves static files and the REST API, and pushes
// text frames to connected WebSocket clients.
//
// Threading model (hardened): the accept thread only accepts and hands each
// connection to its own thread with a receive timeout, so no single peer can
// stall the control plane (slowloris) or the shutdown. Each WebSocket client
// is owned end to end by one thread: broadcast() only enqueues into per-client
// queues (no socket op on the caller's thread), and only the owning thread
// ever closes its socket — eliminating the use-after-close race.
class WsHttpServer {
public:
    static constexpr size_t kMaxClients = 32;
    static constexpr int kRecvTimeoutMs = 5000;
    // How long the accept thread parks per iteration: also the upper bound on
    // how long stop() waits for it to notice and retire.
    static constexpr int kAcceptPollMs = 100;
    static constexpr size_t kMaxQueuedFrames = 120; // ~4 s at 30 Hz; drop-oldest beyond

    ~WsHttpServer();

    bool start(const std::string& bindHost, uint16_t port, std::filesystem::path docRoot);
    void stop();

    // Handler for /api/* requests: (method, path, body) -> JSON response, or
    // empty string for 404. Must be set before start(); called from the
    // accept thread — the handler is responsible for its own thread safety.
    using ApiHandler =
        std::function<std::string(const std::string&, const std::string&, const std::string&)>;
    void setApiHandler(ApiHandler handler) { apiHandler_ = std::move(handler); }

    // Sends a text frame to all connected WebSocket clients. Clients that
    // cannot keep up (send timeout) are dropped — a slow tab must never be
    // able to stall the pipeline thread.
    void broadcast(const std::string& text);

    size_t clientCount() const;

private:
    // One connected WebSocket client, owned by exactly one thread.
    struct Client {
        SocketHandle socket = kInvalidSocket;
        std::mutex mutex;
        std::condition_variable cv;
        std::deque<std::shared_ptr<const std::vector<uint8_t>>> queue;
        bool dead = false;
    };

    void acceptLoop();
    void handleConnection(SocketHandle client);
    void clientLoop(std::shared_ptr<Client> client);
    void serveHttp(SocketHandle client, const std::string& method, const std::string& target,
                   const std::string& body);
    void reapFinished(); // join+drop completed connection threads

    TcpListener listener_;
    std::filesystem::path docRoot_;
    ApiHandler apiHandler_;
    std::thread acceptThread_;
    std::atomic<bool> running_{false};

    mutable std::mutex clientsMutex_;
    std::vector<std::shared_ptr<Client>> clients_;
    // Each connection thread carries a done flag it sets on exit; reaped lazily.
    std::vector<std::pair<std::thread, std::shared_ptr<std::atomic<bool>>>> threads_;
};

} // namespace sillage::net
