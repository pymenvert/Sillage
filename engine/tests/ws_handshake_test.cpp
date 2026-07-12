#include "net/ws_server.h"

#include <gtest/gtest.h>

namespace sillage::net {
namespace {

TEST(WebSocket, Rfc6455HandshakeExample) {
    // Reference vector from RFC 6455 section 1.3.
    EXPECT_EQ(websocketAcceptKey("dGhlIHNhbXBsZSBub25jZQ=="),
              "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=");
}

} // namespace
} // namespace sillage::net
