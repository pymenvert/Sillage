#include "io/osc.h"

#include <gtest/gtest.h>

namespace sillage::osc {
namespace {

TEST(Osc, AddressAndTagsArePaddedToFourBytes) {
    Message msg("/au");
    msg.addInt32(7);
    const auto bytes = msg.encode();

    // "/au\0" (4) + ",i\0\0" (4) + int32 (4)
    ASSERT_EQ(bytes.size(), 12u);
    EXPECT_EQ(bytes[0], '/');
    EXPECT_EQ(bytes[3], 0);
    EXPECT_EQ(bytes[4], ',');
    EXPECT_EQ(bytes[5], 'i');
    EXPECT_EQ(bytes[6], 0);
    EXPECT_EQ(bytes[7], 0);
}

TEST(Osc, Int32IsBigEndian) {
    Message msg("/x");
    msg.addInt32(0x01020304);
    const auto bytes = msg.encode();
    ASSERT_EQ(bytes.size(), 12u);
    EXPECT_EQ(bytes[8], 0x01);
    EXPECT_EQ(bytes[9], 0x02);
    EXPECT_EQ(bytes[10], 0x03);
    EXPECT_EQ(bytes[11], 0x04);
}

TEST(Osc, FloatOneEncodesAsIeee754BigEndian) {
    Message msg("/x");
    msg.addFloat(1.0f);
    const auto bytes = msg.encode();
    ASSERT_EQ(bytes.size(), 12u);
    // 1.0f = 0x3F800000
    EXPECT_EQ(bytes[8], 0x3F);
    EXPECT_EQ(bytes[9], 0x80);
    EXPECT_EQ(bytes[10], 0x00);
    EXPECT_EQ(bytes[11], 0x00);
}

TEST(Osc, SanitizeAddressPartStripsForbiddenChars) {
    // OSC 1.0 forbids space and # * , / ? [ ] { } inside an address.
    EXPECT_EQ(sanitizeAddressPart("main hall"), "main_hall");
    EXPECT_EQ(sanitizeAddressPart("a/b*c?"), "a_b_c_");
    EXPECT_EQ(sanitizeAddressPart("stage#1[left]"), "stage_1_left_");
    EXPECT_EQ(sanitizeAddressPart("clean-name_1"), "clean-name_1");
    EXPECT_EQ(sanitizeAddressPart(""), "_");
    // A sanitized part spliced into an address must contain no forbidden char.
    const std::string addr = "/sillage/zone/" + sanitizeAddressPart("bad /name#") + "/enter";
    for (const char forbidden : {' ', '#', '*', ',', '?', '[', ']', '{', '}'}) {
        EXPECT_EQ(addr.find(forbidden, 14), std::string::npos); // after "/sillage/zone/"
    }
}

TEST(Osc, BundleWrapsMessagesWithSizes) {
    Message a("/a");
    a.addInt32(1);
    const auto bundle = encodeBundle({a.encode()});

    // "#bundle\0" (8) + timetag (8) + size (4) + message (12)
    ASSERT_EQ(bundle.size(), 32u);
    EXPECT_EQ(bundle[0], '#');
    EXPECT_EQ(bundle[15], 1); // immediate timetag
    EXPECT_EQ(bundle[19], 12); // message size
}

} // namespace
} // namespace sillage::osc
