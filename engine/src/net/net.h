#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace sillage::net {

// Minimal portable socket layer (WinSock2 / POSIX). Just enough for M0:
// UDP send (OSC) and a blocking TCP accept/recv/send server (HTTP+WS).

#ifdef _WIN32
using SocketHandle = uintptr_t;
constexpr SocketHandle kInvalidSocket = ~static_cast<SocketHandle>(0);
#else
using SocketHandle = int;
constexpr SocketHandle kInvalidSocket = -1;
#endif

// Process-wide socket subsystem init (WSAStartup on Windows, no-op elsewhere).
void initSockets();

class UdpSender {
public:
    UdpSender() = default;
    ~UdpSender();
    UdpSender(const UdpSender&) = delete;
    UdpSender& operator=(const UdpSender&) = delete;

    bool open(const std::string& host, uint16_t port);
    bool send(const void* data, size_t size);
    void close();

private:
    SocketHandle socket_ = kInvalidSocket;
    std::vector<uint8_t> destAddr_; // sockaddr storage
};

class TcpListener {
public:
    TcpListener() = default;
    ~TcpListener();
    TcpListener(const TcpListener&) = delete;
    TcpListener& operator=(const TcpListener&) = delete;

    bool listen(const std::string& bindHost, uint16_t port);
    // Blocking accept; returns kInvalidSocket on error/close.
    SocketHandle accept();
    void close();

private:
    SocketHandle socket_ = kInvalidSocket;
};

// Blocking helpers on a connected TCP socket.
int tcpRecv(SocketHandle s, void* buffer, size_t size);
bool tcpSendAll(SocketHandle s, const void* data, size_t size);
void tcpClose(SocketHandle s);
void tcpSetSendTimeout(SocketHandle s, int milliseconds);

} // namespace sillage::net
