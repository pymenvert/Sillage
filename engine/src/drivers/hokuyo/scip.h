#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace sillage::scip {

// SCIP 2.2 codec (Hokuyo URG/UST lidars). Text protocol: LF-terminated lines;
// every data-bearing line ends with a 1-char checksum (sum of payload bytes
// & 0x3F, + 0x30); values are 6-bit-per-char encoded, big-endian.
// Reference: Hokuyo "SCIP 2.2 Communication Protocol Specification".

// Encodes an integer into n characters of 6-bit SCIP encoding.
std::string encodeValue(uint32_t value, int chars);

// Decodes n characters of 6-bit encoding; nullopt on malformed input.
std::optional<uint32_t> decodeValue(const char* data, int chars);

// Appends the SCIP checksum character to a payload.
char checksumChar(const std::string& payload);

// Verifies and strips the trailing checksum char; nullopt if invalid.
std::optional<std::string> stripChecksum(const std::string& line);

// Builds an MD command: continuous distance stream.
// start/end are step indices, cluster merges adjacent steps, count 0 = forever.
std::string buildMD(int startStep, int endStep, int cluster, int interval, int count);

// One decoded measurement frame from an MD stream.
struct Frame {
    uint32_t timestampMs = 0;         // 24-bit sensor clock, wraps
    std::vector<uint32_t> distancesMm; // per step; 0..19 are error codes
};

// Parses one complete MD response block (lines between the echo line and the
// terminating empty line). `lines` excludes the echo; lines[0] is the status
// line ("99b" for streaming data). Returns nullopt for non-data blocks (e.g.
// the initial "00P" acknowledge) or corrupt checksums.
std::optional<Frame> parseMDBlock(const std::vector<std::string>& lines);

// Sensor geometry (UST-10LX/20LX defaults: 1081 steps over 270°, front=540).
//
// The resolution must match the step range, or every point lands at the wrong
// bearing. A UST divides the full turn into 1440 steps (0.25°/step), so the
// 1081 steps below span exactly 270°. Using the URG-04LX value (2*pi/1024)
// here stretches the same steps over 379.7°: past a full turn, the polar
// binning in BackgroundModel wraps modulo 2*pi and the outermost steps alias
// onto the bins of the opposite bearing, so their learned background is the
// wrong wall and real people are subtracted away near the room edges.
struct Geometry {
    int startStep = 0;
    int endStep = 1080;
    int frontStep = 540;
    float angularResolution = 0.004363323f; // 2*pi/1440 rad = 0.25°/step
    float angleOfStep(int step) const {
        return static_cast<float>(step - frontStep) * angularResolution;
    }
};

} // namespace sillage::scip
