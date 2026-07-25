#include "drivers/sick/cola.h"

#include <cstdint>
#include <cstdio>
#include <numbers>
#include <sstream>

namespace sillage::cola {

namespace {

constexpr float kTicksToRad =
    std::numbers::pi_v<float> / 180.0f / 10000.0f; // CoLa angles: 1/10000 degree

std::optional<uint32_t> hexToken(const std::string& token) {
    if (token.empty() || token.size() > 8) {
        return std::nullopt;
    }
    uint32_t value = 0;
    for (const char c : token) {
        value <<= 4;
        if (c >= '0' && c <= '9') { value |= static_cast<uint32_t>(c - '0'); }
        else if (c >= 'A' && c <= 'F') { value |= static_cast<uint32_t>(c - 'A' + 10); }
        else if (c >= 'a' && c <= 'f') { value |= static_cast<uint32_t>(c - 'a' + 10); }
        else { return std::nullopt; }
    }
    return value;
}

} // namespace

std::optional<Scan> parseScanTelegram(const std::string& payload) {
    if (payload.rfind("sSN LMDscandata", 0) != 0 && payload.rfind("sRA LMDscandata", 0) != 0) {
        return std::nullopt;
    }
    std::istringstream in(payload);
    std::vector<std::string> tokens;
    std::string token;
    while (in >> token) {
        tokens.push_back(token);
    }
    // Locate the DIST1 channel: DIST1 <scale(8x)> <offset(8x)> <startAngle(8x,
    // signed 1/10000 deg)> <angularStep(4x)> <count(4x)> <count values...>.
    for (size_t i = 0; i + 6 < tokens.size(); ++i) {
        if (tokens[i] != "DIST1") {
            continue;
        }
        const auto startRaw = hexToken(tokens[i + 3]);
        const auto stepRaw = hexToken(tokens[i + 4]);
        const auto count = hexToken(tokens[i + 5]);
        if (!startRaw || !stepRaw || !count || *count == 0 ||
            i + 6 + *count > tokens.size()) {
            return std::nullopt;
        }
        Scan scan;
        scan.startAngleRad = static_cast<float>(static_cast<int32_t>(*startRaw)) * kTicksToRad;
        scan.angularStepRad = static_cast<float>(*stepRaw) * kTicksToRad;
        scan.distancesMm.reserve(*count);
        for (uint32_t v = 0; v < *count; ++v) {
            const auto d = hexToken(tokens[i + 6 + v]);
            if (!d) {
                return std::nullopt;
            }
            scan.distancesMm.push_back(*d);
        }
        return scan;
    }
    return std::nullopt;
}

std::string buildScanTelegram(float startAngleRad, float angularStepRad,
                              const std::vector<uint32_t>& distancesMm) {
    std::string out = "sSN LMDscandata 1 1 0 0 0 0 0 0 0 0 0 1 DIST1 3F800000 00000000";
    char buf[16];
    const auto startTicks = static_cast<int32_t>(startAngleRad / kTicksToRad);
    std::snprintf(buf, sizeof(buf), " %08X", static_cast<uint32_t>(startTicks));
    out += buf;
    std::snprintf(buf, sizeof(buf), " %04X",
                  static_cast<uint32_t>(angularStepRad / kTicksToRad) & 0xFFFF);
    out += buf;
    std::snprintf(buf, sizeof(buf), " %04X", static_cast<uint32_t>(distancesMm.size()));
    out += buf;
    for (const uint32_t d : distancesMm) {
        std::snprintf(buf, sizeof(buf), " %X", d);
        out += buf;
    }
    return out;
}

} // namespace sillage::cola
