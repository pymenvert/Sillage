#include "net/net.h"
#include "net/ws_server.h"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>

namespace sillage::net {
namespace {

// Real-socket integration: HTTP request in, parsed method/path/body out.
class ApiServer : public ::testing::Test {
protected:
    void SetUp() override {
        // A real doc root with one known file, to exercise static serving and
        // path-traversal defenses.
        docRoot = std::filesystem::temp_directory_path() / "sillage_api_test_root";
        std::filesystem::create_directories(docRoot);
        std::ofstream(docRoot / "index.html") << "<h1>ok</h1>";

        server.setApiHandler(
            [](const std::string& method, const std::string& path, const std::string& body) {
                if (path == "/api/echo") {
                    return "{\"method\":\"" + method + "\",\"body\":\"" + body + "\"}";
                }
                return std::string{};
            });
        for (uint16_t p = 18980; p < 19000; ++p) {
            if (server.start("127.0.0.1", p, docRoot)) {
                port = p;
                return;
            }
        }
        FAIL() << "no free port";
    }

    void TearDown() override {
        server.stop();
        std::error_code ec;
        std::filesystem::remove_all(docRoot, ec);
    }

    std::filesystem::path docRoot;

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

TEST_F(ApiServer, ServesFileWithinDocRoot) {
    const std::string r = request("GET / HTTP/1.1\r\nHost: x\r\n\r\n");
    EXPECT_NE(r.find("200 OK"), std::string::npos);
    EXPECT_NE(r.find("<h1>ok</h1>"), std::string::npos);
}

TEST_F(ApiServer, RejectsDotDotTraversal) {
    const std::string r =
        request("GET /../../../../etc/passwd HTTP/1.1\r\nHost: x\r\n\r\n");
    EXPECT_EQ(r.find("200 OK"), std::string::npos);
    EXPECT_NE(r.find("403"), std::string::npos);
}

TEST_F(ApiServer, RejectsWindowsDriveLetterTraversal) {
    // The Windows-specific hole: docRoot_ / "C:/..." resolves OUTSIDE docRoot_.
    const std::string r =
        request("GET /C:/Windows/win.ini HTTP/1.1\r\nHost: x\r\n\r\n");
    EXPECT_EQ(r.find("200 OK"), std::string::npos);
    EXPECT_NE(r.find("403"), std::string::npos);
}

TEST_F(ApiServer, RejectsBackslashTraversal) {
    const std::string r =
        request("GET /..\\..\\secret HTTP/1.1\r\nHost: x\r\n\r\n");
    EXPECT_EQ(r.find("200 OK"), std::string::npos);
}

TEST_F(ApiServer, EmptyTargetDoesNotCrash) {
    // Malformed request line (no target) must yield 400, not std::terminate.
    const std::string r = request("GET\r\nHost: x\r\n\r\n");
    EXPECT_NE(r.find("400"), std::string::npos);
}

TEST_F(ApiServer, SilentClientDoesNotWedgeControlPlane) {
    // Slowloris: one connection that sends nothing must not starve others.
    const SocketHandle idle = tcpConnect("127.0.0.1", port, 2000);
    ASSERT_NE(idle, kInvalidSocket);
    // A normal request must still be served promptly (well under the 5 s recv
    // timeout the idle socket will eventually hit).
    const auto start = std::chrono::steady_clock::now();
    const std::string r = request("GET /api/echo HTTP/1.1\r\nHost: x\r\n\r\n");
    const auto elapsed = std::chrono::steady_clock::now() - start;
    EXPECT_NE(r.find("200 OK"), std::string::npos);
    EXPECT_LT(std::chrono::duration_cast<std::chrono::seconds>(elapsed).count(), 3);
    tcpClose(idle);
}

// stop() must return promptly on a server that never saw a single connection.
// Regression: stop() used to close the listener and then join the accept
// thread, expecting the close to unblock accept(). On Linux it does not, so
// the join waited forever — every fixture in this file hung in TearDown, and
// the Linux CI job burned its whole time budget before being killed. Windows
// and macOS do wake accept() on close, which is why only Linux hung.
TEST(WsHttpServerShutdown, StopsPromptlyWithoutAnyClient) {
    WsHttpServer idle;
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "sillage_shutdown_test_root";
    std::filesystem::create_directories(root);

    bool started = false;
    for (uint16_t p = 19000; p < 19020 && !started; ++p) {
        started = idle.start("127.0.0.1", p, root);
    }
    ASSERT_TRUE(started) << "no free port";

    // Let the accept thread actually park inside accept() before stopping.
    // Without this the thread may still be starting up, observe !running_
    // before its first accept(), and exit for reasons that prove nothing.
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    const auto begin = std::chrono::steady_clock::now();
    idle.stop();
    const auto elapsed = std::chrono::steady_clock::now() - begin;

    EXPECT_LT(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count(), 2000)
        << "stop() must not wait on a connection that never comes";

    std::error_code ec;
    std::filesystem::remove_all(root, ec);
}

} // namespace
} // namespace sillage::net
