#include "drivers/hokuyo/scip.h"

#include <cstdint>
#include <cstdio>

namespace sillage::scip {

std::string encodeValue(uint32_t value, int chars) {
    std::string out(static_cast<size_t>(chars), '0');
    for (int i = chars - 1; i >= 0; --i) {
        out[static_cast<size_t>(i)] = static_cast<char>((value & 0x3F) + 0x30);
        value >>= 6;
    }
    return out;
}

std::optional<uint32_t> decodeValue(const char* data, int chars) {
    uint32_t value = 0;
    for (int i = 0; i < chars; ++i) {
        const int c = static_cast<unsigned char>(data[i]) - 0x30;
        if (c < 0 || c > 0x3F) {
            return std::nullopt;
        }
        value = (value << 6) | static_cast<uint32_t>(c);
    }
    return value;
}

char checksumChar(const std::string& payload) {
    uint32_t sum = 0;
    for (const char c : payload) {
        sum += static_cast<unsigned char>(c);
    }
    return static_cast<char>((sum & 0x3F) + 0x30);
}

std::optional<std::string> stripChecksum(const std::string& line) {
    if (line.empty()) {
        return std::nullopt;
    }
    const std::string payload = line.substr(0, line.size() - 1);
    if (checksumChar(payload) != line.back()) {
        return std::nullopt;
    }
    return payload;
}

std::string buildMD(int startStep, int endStep, int cluster, int interval, int count) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "MD%04d%04d%02d%1d%02d\n", startStep, endStep, cluster,
                  interval, count);
    return buf;
}

std::optional<Frame> parseMDBlock(const std::vector<std::string>& lines) {
    // A data block: status line, timestamp line, then >= 1 data lines.
    if (lines.size() < 3) {
        return std::nullopt;
    }
    const auto status = stripChecksum(lines[0]);
    if (!status || *status != "99") {
        return std::nullopt; // acknowledge block ("00") or error status
    }
    const auto tsPayload = stripChecksum(lines[1]);
    if (!tsPayload || tsPayload->size() != 4) {
        return std::nullopt;
    }
    const auto ts = decodeValue(tsPayload->c_str(), 4);
    if (!ts) {
        return std::nullopt;
    }

    // Concatenate the checksum-stripped data lines, then decode 3-char values.
    std::string data;
    for (size_t i = 2; i < lines.size(); ++i) {
        const auto payload = stripChecksum(lines[i]);
        if (!payload) {
            return std::nullopt;
        }
        data += *payload;
    }
    if (data.size() % 3 != 0) {
        return std::nullopt;
    }

    Frame frame;
    frame.timestampMs = *ts;
    frame.distancesMm.reserve(data.size() / 3);
    for (size_t i = 0; i + 2 < data.size() + 1; i += 3) {
        const auto d = decodeValue(data.c_str() + i, 3);
        if (!d) {
            return std::nullopt;
        }
        frame.distancesMm.push_back(*d);
    }
    return frame;
}

} // namespace sillage::scip
