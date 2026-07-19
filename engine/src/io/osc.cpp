#include "io/osc.h"

#include <bit>
#include <cstring>

namespace sillage::osc {

namespace {

void appendPadded(std::vector<uint8_t>& out, const void* data, size_t size) {
    const auto* p = static_cast<const uint8_t*>(data);
    out.insert(out.end(), p, p + size);
    while (out.size() % 4 != 0) {
        out.push_back(0);
    }
}

void appendString(std::vector<uint8_t>& out, const std::string& s) {
    appendPadded(out, s.c_str(), s.size() + 1); // include NUL, then pad
}

void appendBigEndian32(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back(static_cast<uint8_t>(v >> 24));
    out.push_back(static_cast<uint8_t>(v >> 16));
    out.push_back(static_cast<uint8_t>(v >> 8));
    out.push_back(static_cast<uint8_t>(v));
}

} // namespace

Message& Message::addInt32(int32_t v) {
    typeTags_ += 'i';
    appendBigEndian32(args_, static_cast<uint32_t>(v));
    return *this;
}

Message& Message::addFloat(float v) {
    typeTags_ += 'f';
    appendBigEndian32(args_, std::bit_cast<uint32_t>(v));
    return *this;
}

Message& Message::addString(const std::string& v) {
    typeTags_ += 's';
    appendString(args_, v);
    return *this;
}

std::vector<uint8_t> Message::encode() const {
    std::vector<uint8_t> out;
    out.reserve(address_.size() + typeTags_.size() + args_.size() + 16);
    appendString(out, address_);
    appendString(out, typeTags_);
    out.insert(out.end(), args_.begin(), args_.end());
    return out;
}

std::string sanitizeAddressPart(const std::string& part) {
    std::string out;
    out.reserve(part.size());
    for (const char c : part) {
        const bool forbidden = c == ' ' || c == '#' || c == '*' || c == ',' || c == '/' ||
                               c == '?' || c == '[' || c == ']' || c == '{' || c == '}' ||
                               static_cast<unsigned char>(c) < 0x20;
        out += forbidden ? '_' : c;
    }
    return out.empty() ? "_" : out;
}

std::vector<uint8_t> encodeBundle(const std::vector<std::vector<uint8_t>>& messages) {
    std::vector<uint8_t> out;
    appendString(out, "#bundle");
    // Immediate timetag (1).
    appendBigEndian32(out, 0);
    appendBigEndian32(out, 1);
    for (const auto& m : messages) {
        appendBigEndian32(out, static_cast<uint32_t>(m.size()));
        out.insert(out.end(), m.begin(), m.end());
    }
    return out;
}

} // namespace sillage::osc
