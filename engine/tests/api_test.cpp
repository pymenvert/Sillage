#include "net/net.h"
#include "net/ws_server.h"

#include <gtest/gtest.h>

#include <chrono>
#include <thread>

namespace sillage::net {
namespace {

// Real-socket integration: HTTP request in, parsed method/path/body out.
class ApiServer : public ::testing::Test {
protected:
    void SetUp() override {
        server.setApiHandler(
            [](const std::string& method, const std::string& path, const std::string& body) {
                if (path == "/api/echo") {
                    return "{\"method\":\"" + method + "\",\"body\":\"" + body + "\"}";
                }
                return std::string{};
            });
        for (uint16_t p = 18980; p < 19000; ++p) {
            if (server.start("127.0.0.1", p, ".")) {
                port = p;
                return;
            }
        }
        FAIL() << "no free port";
    }

    std::string request(const std::string& raw) {
        const SocketHandle s = tcpConnect("127.0.0.1", port, 2000);
        EXPECT_NE(s, kInvalidSocket);
        EXPECT_TRUE(tcpSendAll(s, raw.data(), raw.size()));
        std::string response;
        char buf[4096];
        int n;
        while ((n = tcpRecv(s, buf, sizeof(buf))) > 0) {
            response.append(buf, static_cast<size_t>(n));
        }
        tcpClose(s);
        return response;
    }

    WsHttpServer server;
    uint16_t port = 0;
};

TEST_F(ApiServer, GetRoutesThroughHandler) {
    const std::string r = request("GET /api/echo HTTP/1.1\r\nHost: x\r\n\r\n");
    EXPECT_NE(r.find("200 OK"), std::string::npos);
    EXPECT_NE(r.find("\"method\":\"GET\""), std::string::npos);
}

TEST_F(ApiServer, PostBodyIsDeliveredCompletely) {
    const std::string payload = "hello-config-payload";
    const std::string r = request("POST /api/echo HTTP/1.1\r\nHost: x\r\nContent-Length: " +
                                  std::to_string(payload.size()) + "\r\n\r\n" + payload);
    EXPECT_NE(r.find("200 OK"), std::string::npos);
    EXPECT_NE(r.find("\"body\":\"" + payload + "\""), std::string::npos);
}

TEST_F(ApiServer, UnknownApiPathIs404) {
    const std::string r = request("GET /api/nope HTTP/1.1\r\nHost: x\r\n\r\n");
    EXPECT_NE(r.find("404"), std::string::npos);
}

TEST_F(ApiServer, OversizedBodyIsRejected) {
    // Header claims 2 MB: the server must drop the connection, not buffer it.
    const std::string r =
        request("POST /api/echo HTTP/1.1\r\nHost: x\r\nContent-Length: 2097152\r\n\r\nxx");
    EXPECT_EQ(r.find("200 OK"), std::string::npos);
}

} // namespace
} // namespace sillage::net
