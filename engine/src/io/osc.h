#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace sillage::osc {

// Minimal OSC 1.0 message/bundle encoder. Big-endian, 4-byte aligned.
class Message {
public:
    explicit Message(std::string address) : address_(std::move(address)) {}

    Message& addInt32(int32_t v);
    Message& addFloat(float v);
    Message& addString(const std::string& v);

    // Serialized message (address + type tags + arguments), padded.
    std::vector<uint8_t> encode() const;

private:
    std::string address_;
    std::string typeTags_ = ",";
    std::vector<uint8_t> args_;
};

// Wraps encoded messages into an OSC bundle with immediate timetag.
std::vector<uint8_t> encodeBundle(const std::vector<std::vector<uint8_t>>& messages);

// Makes a string safe to splice into an OSC address: the OSC 1.0 grammar
// forbids space and the characters # * , / ? [ ] { } (and control chars).
// Forbidden characters become '_'; an empty result becomes "_".
std::string sanitizeAddressPart(const std::string& part);

} // namespace sillage::osc
