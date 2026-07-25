#include "net/net.h"

#include <cstdint>
#include <cstring>
#include <mutex>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
using socklen_type = int;
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
using socklen_type = socklen_t;
#endif

namespace sillage::net {

namespace {

#ifdef _WIN32
SOCKET toNative(SocketHandle s) { return static_cast<SOCKET>(s); }
#else
int toNative(SocketHandle s) { return s; }
#endif

bool resolveIpv4(const std::string& host, uint16_t port, sockaddr_in& out) {
    std::memset(&out, 0, sizeof(out));
    out.sin_family = AF_INET;
    out.sin_port = htons(port);
    if (inet_pton(AF_INET, host.c_str(), &out.sin_addr) == 1) {
        return true;
    }
    addrinfo hints{};
    hints.ai_family = AF_INET;
    addrinfo* res = nullptr;
    if (getaddrinfo(host.c_str(), nullptr, &hints, &res) != 0 || res == nullptr) {
        return false;
    }
    out.sin_addr = reinterpret_cast<sockaddr_in*>(res->ai_addr)->sin_addr;
    freeaddrinfo(res);
    return true;
}

} // namespace

void initSockets() {
#ifdef _WIN32
    static std::once_flag once;
    std::call_once(once, [] {
        WSADATA data;
        WSAStartup(MAKEWORD(2, 2), &data);
    });
#endif
}

// --- UdpSender ---------------------------------------------------------------

UdpSender::~UdpSender() { close(); }

bool UdpSender::open(const std::string& host, uint16_t port) {
    close(); // reopening with a new destination must not leak the old socket
    initSockets();
    sockaddr_in dest{};
    if (!resolveIpv4(host, port, dest)) {
        return false;
    }
    const auto s = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
#ifdef _WIN32
    if (s == INVALID_SOCKET) {
        return false;
    }
#else
    if (s < 0) {
        return false;
    }
#endif
    socket_ = static_cast<SocketHandle>(s);
    destAddr_.resize(sizeof(dest));
    std::memcpy(destAddr_.data(), &dest, sizeof(dest));
    return true;
}

bool UdpSender::send(const void* data, size_t size) {
    if (socket_ == kInvalidSocket) {
        return false;
    }
    const auto sent =
        ::sendto(toNative(socket_), static_cast<const char*>(data), static_cast<int>(size), 0,
                 reinterpret_cast<const sockaddr*>(destAddr_.data()),
                 static_cast<socklen_type>(destAddr_.size()));
    return sent == static_cast<decltype(sent)>(size);
}

void UdpSender::close() {
    if (socket_ != kInvalidSocket) {
        tcpClose(socket_);
        socket_ = kInvalidSocket;
    }
}

// --- UdpReceiver -------------------------------------------------------------

bool UdpReceiver::bind(const std::string& bindHost, uint16_t port, int recvTimeoutMs) {
    close();
    initSockets();
    sockaddr_in addr{};
    if (!resolveIpv4(bindHost, port, addr)) {
        return false;
    }
    const auto s = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
#ifdef _WIN32
    if (s == INVALID_SOCKET) {
        return false;
    }
#else
    if (s < 0) {
        return false;
    }
#endif
    if (::bind(s, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) != 0) {
        tcpClose(static_cast<SocketHandle>(s));
        return false;
    }
    tcpSetRecvTimeout(static_cast<SocketHandle>(s), recvTimeoutMs);
    socket_ = static_cast<SocketHandle>(s);
    return true;
}

int UdpReceiver::receive(void* buffer, size_t size) {
    if (socket_ == kInvalidSocket) {
        return -1;
    }
    return static_cast<int>(
        ::recvfrom(toNative(socket_), static_cast<char*>(buffer), static_cast<int>(size), 0,
                   nullptr, nullptr));
}

void UdpReceiver::close() {
    if (socket_ != kInvalidSocket) {
        tcpClose(socket_);
        socket_ = kInvalidSocket;
    }
}

// --- TcpListener -------------------------------------------------------------

TcpListener::~TcpListener() { close(); }

bool TcpListener::listen(const std::string& bindHost, uint16_t port) {
    initSockets();
    sockaddr_in addr{};
    if (!resolveIpv4(bindHost, port, addr)) {
        return false;
    }
    const auto s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
#ifdef _WIN32
    if (s == INVALID_SOCKET) {
        return false;
    }
#else
    if (s < 0) {
        return false;
    }
#endif
    const int yes = 1;
    ::setsockopt(s, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&yes), sizeof(yes));
    if (::bind(s, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) != 0 ||
        ::listen(s, 8) != 0) {
        socket_ = static_cast<SocketHandle>(s);
        close();
        return false;
    }
    socket_ = static_cast<SocketHandle>(s);
    return true;
}

SocketHandle TcpListener::accept() {
    if (socket_ == kInvalidSocket) {
        return kInvalidSocket;
    }
    const auto client = ::accept(toNative(socket_), nullptr, nullptr);
#ifdef _WIN32
    if (client == INVALID_SOCKET) {
        return kInvalidSocket;
    }
#else
    if (client < 0) {
        return kInvalidSocket;
    }
#endif
    return static_cast<SocketHandle>(client);
}

void TcpListener::close() {
    if (socket_ != kInvalidSocket) {
        tcpClose(socket_);
        socket_ = kInvalidSocket;
    }
}

// --- TCP helpers -------------------------------------------------------------

SocketHandle tcpConnect(const std::string& host, uint16_t port, int timeoutMs) {
    initSockets();
    sockaddr_in addr{};
    if (!resolveIpv4(host, port, addr)) {
        return kInvalidSocket;
    }
    const auto s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
#ifdef _WIN32
    if (s == INVALID_SOCKET) {
        return kInvalidSocket;
    }
#else
    if (s < 0) {
        return kInvalidSocket;
    }
#endif
    // Bounded connect: non-blocking connect + select, so a dead host does not
    // park the driver thread for the OS default (~20 s) and, worse, make
    // stop() wait that long. Falls back to blocking on setup failure.
    setNonBlocking(static_cast<SocketHandle>(s), true);
    const int rc = ::connect(s, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr));
    bool connected = (rc == 0);
    if (!connected) {
        fd_set writable;
        FD_ZERO(&writable);
        FD_SET(s, &writable);
        timeval tv{};
        tv.tv_sec = timeoutMs / 1000;
        tv.tv_usec = (timeoutMs % 1000) * 1000;
        if (::select(static_cast<int>(s) + 1, nullptr, &writable, nullptr, &tv) > 0) {
            int err = 0;
            socklen_type len = sizeof(err);
            ::getsockopt(s, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&err), &len);
            connected = (err == 0);
        }
    }
    if (!connected) {
        tcpClose(static_cast<SocketHandle>(s));
        return kInvalidSocket;
    }
    setNonBlocking(static_cast<SocketHandle>(s), false);
    tcpSetRecvTimeout(static_cast<SocketHandle>(s), timeoutMs);
    return static_cast<SocketHandle>(s);
}

int waitReadable(SocketHandle s, int timeoutMs) {
    if (s == kInvalidSocket) {
        return -1;
    }
    fd_set readable;
    FD_ZERO(&readable);
    FD_SET(toNative(s), &readable);
    timeval tv{};
    tv.tv_sec = timeoutMs / 1000;
    tv.tv_usec = (timeoutMs % 1000) * 1000;
    return ::select(static_cast<int>(toNative(s)) + 1, &readable, nullptr, nullptr, &tv);
}

void setNonBlocking(SocketHandle s, bool nonBlocking) {
#ifdef _WIN32
    u_long mode = nonBlocking ? 1 : 0;
    ::ioctlsocket(toNative(s), FIONBIO, &mode);
#else
    const int flags = ::fcntl(toNative(s), F_GETFL, 0);
    ::fcntl(toNative(s), F_SETFL, nonBlocking ? (flags | O_NONBLOCK) : (flags & ~O_NONBLOCK));
#endif
}

void tcpSetRecvTimeout(SocketHandle s, int milliseconds) {
#ifdef _WIN32
    const DWORD timeout = static_cast<DWORD>(milliseconds);
    ::setsockopt(toNative(s), SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout),
                 sizeof(timeout));
#else
    timeval tv{};
    tv.tv_sec = milliseconds / 1000;
    tv.tv_usec = (milliseconds % 1000) * 1000;
    ::setsockopt(toNative(s), SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif
}

int tcpRecv(SocketHandle s, void* buffer, size_t size) {
    return static_cast<int>(
        ::recv(toNative(s), static_cast<char*>(buffer), static_cast<int>(size), 0));
}

bool tcpSendAll(SocketHandle s, const void* data, size_t size) {
    const char* p = static_cast<const char*>(data);
    size_t remaining = size;
    while (remaining > 0) {
        const auto sent = ::send(toNative(s), p, static_cast<int>(remaining), 0);
        if (sent <= 0) {
            return false;
        }
        p += sent;
        remaining -= static_cast<size_t>(sent);
    }
    return true;
}

void tcpClose(SocketHandle s) {
#ifdef _WIN32
    ::closesocket(toNative(s));
#else
    ::close(toNative(s));
#endif
}

void tcpSetSendTimeout(SocketHandle s, int milliseconds) {
#ifdef _WIN32
    const DWORD timeout = static_cast<DWORD>(milliseconds);
    ::setsockopt(toNative(s), SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&timeout),
                 sizeof(timeout));
#else
    timeval tv{};
    tv.tv_sec = milliseconds / 1000;
    tv.tv_usec = (milliseconds % 1000) * 1000;
    ::setsockopt(toNative(s), SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
#endif
}

} // namespace sillage::net
