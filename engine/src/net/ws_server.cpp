#include "net/ws_server.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <fstream>
#include <sstream>

namespace sillage::net {

namespace {

// --- SHA-1 (RFC 3174), compact implementation for the WS handshake only ------

struct Sha1 {
    uint32_t h[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};
    uint64_t totalBits = 0;
    std::array<uint8_t, 64> block{};
    size_t blockLen = 0;

    static uint32_t rol(uint32_t v, int bits) { return (v << bits) | (v >> (32 - bits)); }

    void processBlock() {
        uint32_t w[80];
        for (int i = 0; i < 16; ++i) {
            w[i] = (static_cast<uint32_t>(block[i * 4]) << 24) |
                   (static_cast<uint32_t>(block[i * 4 + 1]) << 16) |
                   (static_cast<uint32_t>(block[i * 4 + 2]) << 8) |
                   static_cast<uint32_t>(block[i * 4 + 3]);
        }
        for (int i = 16; i < 80; ++i) {
            w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        }
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
        for (int i = 0; i < 80; ++i) {
            uint32_t f, k;
            if (i < 20) {
                f = (b & c) | (~b & d);
                k = 0x5A827999;
            } else if (i < 40) {
                f = b ^ c ^ d;
                k = 0x6ED9EBA1;
            } else if (i < 60) {
                f = (b & c) | (b & d) | (c & d);
                k = 0x8F1BBCDC;
            } else {
                f = b ^ c ^ d;
                k = 0xCA62C1D6;
            }
            const uint32_t tmp = rol(a, 5) + f + e + k + w[i];
            e = d;
            d = c;
            c = rol(b, 30);
            b = a;
            a = tmp;
        }
        h[0] += a;
        h[1] += b;
        h[2] += c;
        h[3] += d;
        h[4] += e;
    }

    void update(const void* data, size_t size) {
        const auto* p = static_cast<const uint8_t*>(data);
        totalBits += static_cast<uint64_t>(size) * 8;
        while (size > 0) {
            const size_t take = std::min(size, block.size() - blockLen);
            std::memcpy(block.data() + blockLen, p, take);
            blockLen += take;
            p += take;
            size -= take;
            if (blockLen == block.size()) {
                processBlock();
                blockLen = 0;
            }
        }
    }

    std::array<uint8_t, 20> finish() {
        const uint64_t bits = totalBits; // captured before padding is appended
        const uint8_t pad = 0x80;
        update(&pad, 1);
        const uint8_t zero = 0;
        while (blockLen != 56) {
            update(&zero, 1);
        }
        uint8_t len[8];
        for (int i = 0; i < 8; ++i) {
            len[i] = static_cast<uint8_t>(bits >> (56 - i * 8));
        }
        update(len, 8);
        std::array<uint8_t, 20> digest{};
        for (int i = 0; i < 5; ++i) {
            digest[i * 4] = static_cast<uint8_t>(h[i] >> 24);
            digest[i * 4 + 1] = static_cast<uint8_t>(h[i] >> 16);
            digest[i * 4 + 2] = static_cast<uint8_t>(h[i] >> 8);
            digest[i * 4 + 3] = static_cast<uint8_t>(h[i]);
        }
        return digest;
    }
};

std::string base64(const uint8_t* data, size_t size) {
    static const char* table = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((size + 2) / 3 * 4);
    for (size_t i = 0; i < size; i += 3) {
        const uint32_t b0 = data[i];
        const uint32_t b1 = i + 1 < size ? data[i + 1] : 0;
        const uint32_t b2 = i + 2 < size ? data[i + 2] : 0;
        const uint32_t triple = (b0 << 16) | (b1 << 8) | b2;
        out.push_back(table[(triple >> 18) & 0x3F]);
        out.push_back(table[(triple >> 12) & 0x3F]);
        out.push_back(i + 1 < size ? table[(triple >> 6) & 0x3F] : '=');
        out.push_back(i + 2 < size ? table[triple & 0x3F] : '=');
    }
    return out;
}

std::string headerValue(const std::string& request, const std::string& name) {
    // Case-insensitive header lookup.
    std::string lower = request;
    std::string lname = name;
    for (char& ch : lower) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    for (char& ch : lname) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    const size_t pos = lower.find("\r\n" + lname + ":");
    if (pos == std::string::npos) {
        return {};
    }
    const size_t start = pos + 2 + lname.size() + 1;
    const size_t end = lower.find("\r\n", start);
    std::string value = request.substr(start, end - start);
    const size_t first = value.find_first_not_of(' ');
    const size_t last = value.find_last_not_of(" \r");
    if (first == std::string::npos) {
        return {};
    }
    return value.substr(first, last - first + 1);
}

std::vector<uint8_t> wsTextFrame(const std::string& text) {
    std::vector<uint8_t> frame;
    frame.reserve(text.size() + 10);
    frame.push_back(0x81); // FIN + text opcode
    const size_t n = text.size();
    if (n < 126) {
        frame.push_back(static_cast<uint8_t>(n));
    } else if (n < 65536) {
        frame.push_back(126);
        frame.push_back(static_cast<uint8_t>(n >> 8));
        frame.push_back(static_cast<uint8_t>(n));
    } else {
        frame.push_back(127);
        for (int i = 7; i >= 0; --i) {
            frame.push_back(static_cast<uint8_t>(static_cast<uint64_t>(n) >> (i * 8)));
        }
    }
    frame.insert(frame.end(), text.begin(), text.end());
    return frame;
}

} // namespace

std::string websocketAcceptKey(const std::string& clientKey) {
    static const char* guid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    Sha1 sha;
    sha.update(clientKey.data(), clientKey.size());
    sha.update(guid, std::strlen(guid));
    const auto digest = sha.finish();
    return base64(digest.data(), digest.size());
}

WsHttpServer::~WsHttpServer() { stop(); }

bool WsHttpServer::start(const std::string& bindHost, uint16_t port,
                         std::filesystem::path docRoot) {
    docRoot_ = std::move(docRoot);
    if (!listener_.listen(bindHost, port)) {
        return false;
    }
    running_ = true;
    acceptThread_ = std::thread([this] { acceptLoop(); });
    return true;
}

void WsHttpServer::stop() {
    if (!running_.exchange(false)) {
        return;
    }
    listener_.close(); // unblocks accept()
    if (acceptThread_.joinable()) {
        acceptThread_.join();
    }
    {
        std::lock_guard lock(clientsMutex_);
        for (const SocketHandle c : clients_) {
            tcpClose(c);
        }
        clients_.clear();
    }
    for (std::thread& t : readerThreads_) {
        if (t.joinable()) {
            t.join();
        }
    }
    readerThreads_.clear();
}

size_t WsHttpServer::clientCount() const {
    std::lock_guard lock(clientsMutex_);
    return clients_.size();
}

void WsHttpServer::acceptLoop() {
    while (running_) {
        const SocketHandle client = listener_.accept();
        if (client == kInvalidSocket) {
            continue; // listener closed on stop(), loop exits via running_
        }
        handleConnection(client);
    }
}

void WsHttpServer::handleConnection(SocketHandle client) {
    // Read request headers.
    std::string request;
    char buf[2048];
    while (request.find("\r\n\r\n") == std::string::npos) {
        const int n = tcpRecv(client, buf, sizeof(buf));
        if (n <= 0) {
            tcpClose(client);
            return;
        }
        request.append(buf, static_cast<size_t>(n));
        if (request.size() > 16384) {
            tcpClose(client);
            return;
        }
    }

    std::istringstream firstLine(request.substr(0, request.find("\r\n")));
    std::string method, target;
    firstLine >> method >> target;

    const std::string wsKey = headerValue(request, "Sec-WebSocket-Key");
    if (!wsKey.empty()) {
        const std::string response = "HTTP/1.1 101 Switching Protocols\r\n"
                                     "Upgrade: websocket\r\n"
                                     "Connection: Upgrade\r\n"
                                     "Sec-WebSocket-Accept: " +
                                     websocketAcceptKey(wsKey) + "\r\n\r\n";
        if (!tcpSendAll(client, response.data(), response.size())) {
            tcpClose(client);
            return;
        }
        tcpSetSendTimeout(client, 200);
        {
            std::lock_guard lock(clientsMutex_);
            clients_.push_back(client);
        }
        // Reader drains client frames so the socket stays healthy; close frame
        // or error removes the client.
        readerThreads_.emplace_back([this, client] {
            char drain[1024];
            while (running_) {
                const int n = tcpRecv(client, drain, sizeof(drain));
                if (n <= 0) {
                    break;
                }
            }
            std::lock_guard lock(clientsMutex_);
            std::erase(clients_, client);
        });
        return;
    }

    serveHttp(client, target);
    tcpClose(client);
}

void WsHttpServer::serveHttp(SocketHandle client, const std::string& target) {
    std::string path = target == "/" ? "/index.html" : target;
    // Reject path traversal outright.
    if (path.find("..") != std::string::npos) {
        const std::string resp = "HTTP/1.1 403 Forbidden\r\nContent-Length: 0\r\n\r\n";
        tcpSendAll(client, resp.data(), resp.size());
        return;
    }

    const std::filesystem::path file = docRoot_ / path.substr(1);
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        const std::string resp = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n";
        tcpSendAll(client, resp.data(), resp.size());
        return;
    }
    std::stringstream content;
    content << in.rdbuf();
    const std::string body = content.str();

    const char* contentType = "application/octet-stream";
    if (file.extension() == ".html") {
        contentType = "text/html; charset=utf-8";
    } else if (file.extension() == ".js") {
        contentType = "text/javascript";
    } else if (file.extension() == ".css") {
        contentType = "text/css";
    }

    std::string header = "HTTP/1.1 200 OK\r\nContent-Type: " + std::string(contentType) +
                         "\r\nContent-Length: " + std::to_string(body.size()) +
                         "\r\nCache-Control: no-store\r\n\r\n";
    tcpSendAll(client, header.data(), header.size());
    tcpSendAll(client, body.data(), body.size());
}

void WsHttpServer::broadcast(const std::string& text) {
    const std::vector<uint8_t> frame = wsTextFrame(text);
    std::lock_guard lock(clientsMutex_);
    std::erase_if(clients_, [&](const SocketHandle c) {
        if (!tcpSendAll(c, frame.data(), frame.size())) {
            tcpClose(c);
            return true;
        }
        return false;
    });
}

} // namespace sillage::net
