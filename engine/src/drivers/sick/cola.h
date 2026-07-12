#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace sillage::cola {

// SICK CoLa A (ASCII) codec for TiM 5xx/7xx scan data. Telegrams are framed
// <STX>payload<ETX>; LMDscandata payloads carry space-separated tokens with
// hex-encoded values. Parsing anchors on the "DIST1" channel marker instead
// of absolute field positions — firmware families vary their header lengths.

constexpr char kStx = '\x02';
constexpr char kEtx = '\x03';

inline std::string frame(const std::string& payload) { return kStx + payload + kEtx; }

// Subscription command: the device then pushes "sSN LMDscandata" telegrams.
inline std::string subscribeScans() { return frame("sEN LMDscandata 1"); }

struct Scan {
    float startAngleRad = 0.0f;
    float angularStepRad = 0.0f;
    std::vector<uint32_t> distancesMm;
};

// Parses one LMDscandata payload (without STX/ETX). Accepts both the
// subscription push ("sSN LMDscandata ...") and the polled reply
// ("sRA LMDscandata ..."). nullopt if no valid DIST1 block is found.
std::optional<Scan> parseScanTelegram(const std::string& payload);

// Builds a well-formed LMDscandata payload (mock device + tests).
std::string buildScanTelegram(float startAngleRad, float angularStepRad,
                              const std::vector<uint32_t>& distancesMm);

} // namespace sillage::cola
