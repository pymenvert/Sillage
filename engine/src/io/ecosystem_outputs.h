#pragma once

#include "core/types.h"
#include "io/osc.h"
#include "net/net.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace sillage {

// TUIO 1.1, profile 2Dcur: every confirmed person is a cursor. Coordinates
// normalized to the room; TUIO convention has y growing downward, so y is
// flipped. Consumed by mapping/multitouch software.
class TuioOutput {
public:
    bool open(const std::string& host, uint16_t port) { return sender_.open(host, port); }
    void publish(const FrameSnapshot& snap);

private:
    net::UdpSender sender_;
    uint32_t frameSeq_ = 0;
};

// ADM-OSC (immersive-audio-live/ADM-OSC): tracked people drive audio objects
// in L-ISA / SPAT Revolution / d&b Soundscape. Position mapped to the
// normalized [-1,1] cartesian convention; object number = oid + 1, capped.
class AdmOscOutput {
public:
    bool open(const std::string& host, uint16_t port, uint32_t maxObjects) {
        maxObjects_ = maxObjects;
        return sender_.open(host, port);
    }
    void publish(const FrameSnapshot& snap);

private:
    net::UdpSender sender_;
    uint32_t maxObjects_ = 16;
};

// Output conditioning (docs/03 §9): optional latency-compensating
// extrapolation and optional One-Euro smoothing, applied to the published
// copy only — the tracker's state is never touched.
class OutputConditioner {
public:
    struct Params {
        float predictionSeconds = 0.0f;
        bool smoothing = false;
        // One-Euro tuning (defaults from the original paper, adapted to meters).
        float minCutoff = 1.0f;
        float beta = 0.3f;
    };

    explicit OutputConditioner(Params params) : params_(params) {}

    std::vector<Track> apply(const std::vector<Track>& tracks, float dt);

private:
    struct Axis {
        float value = 0.0f;
        float derivative = 0.0f;
        bool initialized = false;
    };
    struct Filter {
        Axis x, y;
        uint64_t lastSeen = 0;
    };
    float filterAxis(Axis& axis, float raw, float dt) const;

    Params params_;
    std::map<uint32_t, Filter> filters_;
    uint64_t tick_ = 0;
};

} // namespace sillage
