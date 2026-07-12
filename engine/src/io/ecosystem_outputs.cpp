#include "io/ecosystem_outputs.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace sillage {

void TuioOutput::publish(const FrameSnapshot& snap) {
    const float w = snap.roomSize.x > 0.0f ? snap.roomSize.x : 1.0f;
    const float h = snap.roomSize.y > 0.0f ? snap.roomSize.y : 1.0f;

    std::vector<std::vector<uint8_t>> messages;

    osc::Message alive("/tuio/2Dcur");
    alive.addString("alive");
    for (const Track& t : snap.tracks) {
        alive.addInt32(static_cast<int32_t>(t.id));
    }
    messages.push_back(alive.encode());

    for (const Track& t : snap.tracks) {
        osc::Message set("/tuio/2Dcur");
        set.addString("set")
            .addInt32(static_cast<int32_t>(t.id))
            .addFloat(std::clamp(t.position.x / w, 0.0f, 1.0f))
            .addFloat(std::clamp(1.0f - t.position.y / h, 0.0f, 1.0f)) // TUIO y is down
            .addFloat(t.velocity.x / w)
            .addFloat(-t.velocity.y / h)
            .addFloat(0.0f); // acceleration: not estimated
        messages.push_back(set.encode());
    }

    osc::Message fseq("/tuio/2Dcur");
    fseq.addString("fseq").addInt32(static_cast<int32_t>(++frameSeq_));
    messages.push_back(fseq.encode());

    const auto bundle = osc::encodeBundle(messages);
    sender_.send(bundle.data(), bundle.size());
}

void AdmOscOutput::publish(const FrameSnapshot& snap) {
    const float w = snap.roomSize.x > 0.0f ? snap.roomSize.x : 1.0f;
    const float h = snap.roomSize.y > 0.0f ? snap.roomSize.y : 1.0f;

    std::vector<std::vector<uint8_t>> messages;
    for (const Track& t : snap.tracks) {
        if (t.oid >= maxObjects_) {
            continue;
        }
        osc::Message msg("/adm/obj/" + std::to_string(t.oid + 1) + "/xyz");
        msg.addFloat(std::clamp(t.position.x / w * 2.0f - 1.0f, -1.0f, 1.0f))
            .addFloat(std::clamp(t.position.y / h * 2.0f - 1.0f, -1.0f, 1.0f))
            .addFloat(0.0f);
        messages.push_back(msg.encode());
    }
    if (messages.empty()) {
        return;
    }
    const auto bundle = osc::encodeBundle(messages);
    sender_.send(bundle.data(), bundle.size());
}

float OutputConditioner::filterAxis(Axis& axis, float raw, float dt) const {
    if (!axis.initialized) {
        axis.value = raw;
        axis.derivative = 0.0f;
        axis.initialized = true;
        return raw;
    }
    const auto alpha = [dt](float cutoff) {
        const float tau = 1.0f / (2.0f * std::numbers::pi_v<float> * cutoff);
        return 1.0f / (1.0f + tau / dt);
    };
    const float dRaw = (raw - axis.value) / dt;
    const float aD = alpha(1.0f); // derivative cutoff 1 Hz
    axis.derivative += aD * (dRaw - axis.derivative);
    const float cutoff = params_.minCutoff + params_.beta * std::abs(axis.derivative);
    const float a = alpha(cutoff);
    axis.value += a * (raw - axis.value);
    return axis.value;
}

std::vector<Track> OutputConditioner::apply(const std::vector<Track>& tracks, float dt) {
    ++tick_;
    std::vector<Track> out = tracks;
    for (Track& t : out) {
        if (params_.predictionSeconds > 0.0f) {
            t.position = t.position + t.velocity * params_.predictionSeconds;
        }
        if (params_.smoothing) {
            Filter& f = filters_[t.id];
            f.lastSeen = tick_;
            t.position.x = filterAxis(f.x, t.position.x, dt);
            t.position.y = filterAxis(f.y, t.position.y, dt);
        }
    }
    // Drop filters of departed tracks.
    std::erase_if(filters_, [&](const auto& kv) { return tick_ - kv.second.lastSeen > 120; });
    return out;
}

} // namespace sillage
