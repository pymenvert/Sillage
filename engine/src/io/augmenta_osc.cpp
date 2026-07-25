#include "io/augmenta_osc.h"

#include "io/osc.h"

#include <algorithm>
#include <cstdint>

namespace sillage {

namespace {

// Legacy Augmenta person message: coordinates normalized to [0,1] in scene
// space, bounding rect approximated from the track's radius at M0.
std::vector<uint8_t> personMessage(const char* address, const Track& t,
                                   const FrameSnapshot& snap) {
    const float w = snap.roomSize.x > 0.0f ? snap.roomSize.x : 1.0f;
    const float h = snap.roomSize.y > 0.0f ? snap.roomSize.y : 1.0f;
    const float nx = std::clamp(t.position.x / w, 0.0f, 1.0f);
    const float ny = std::clamp(t.position.y / h, 0.0f, 1.0f);
    const float nr = t.radius / std::max(w, h);

    osc::Message msg(address);
    msg.addInt32(static_cast<int32_t>(t.id))                       // pid
        .addInt32(static_cast<int32_t>(t.oid))                     // oid
        .addInt32(static_cast<int32_t>(snap.tick - t.bornTick))    // age (frames)
        .addFloat(nx)                                              // centroid.x
        .addFloat(ny)                                              // centroid.y
        .addFloat(t.velocity.x / w)                                // velocity.x (normalized/s)
        .addFloat(t.velocity.y / h)                                // velocity.y
        .addFloat(0.0f)                                            // depth (n/a in 2D)
        .addFloat(std::clamp(nx - nr, 0.0f, 1.0f))                 // boundingRect.x
        .addFloat(std::clamp(ny - nr, 0.0f, 1.0f))                 // boundingRect.y
        .addFloat(2.0f * nr)                                       // boundingRect.width
        .addFloat(2.0f * nr)                                       // boundingRect.height
        .addFloat(nx)                                              // highest.x
        .addFloat(ny)                                              // highest.y
        .addFloat(0.0f);                                           // highest.z (height, n/a)
    return msg.encode();
}

} // namespace

bool AugmentaOscOutput::open(const std::string& host, uint16_t port) {
    return sender_.open(host, port);
}

void AugmentaOscOutput::publish(const FrameSnapshot& snap) {
    std::vector<std::vector<uint8_t>> messages;

    std::unordered_set<uint32_t> liveIds;
    for (const Track& t : snap.tracks) {
        liveIds.insert(t.id);
        const bool isNew = knownIds_.insert(t.id).second;
        messages.push_back(personMessage(isNew ? "/au/personEntered" : "/au/personUpdated", t,
                                         snap));
    }

    // personWillLeave for ids that disappeared since the previous frame.
    for (auto it = knownIds_.begin(); it != knownIds_.end();) {
        if (!liveIds.contains(*it)) {
            Track ghost;
            ghost.id = *it;
            messages.push_back(personMessage("/au/personWillLeave", ghost, snap));
            it = knownIds_.erase(it);
        } else {
            ++it;
        }
    }

    // Scene message.
    osc::Message scene("/au/scene");
    scene.addInt32(static_cast<int32_t>(snap.tick))               // age (frames)
        .addFloat(0.0f)                                           // percentCovered (M1)
        .addInt32(static_cast<int32_t>(snap.tracks.size()))       // numPeople
        .addFloat(0.0f)                                           // averageMotion.x (M1)
        .addFloat(0.0f)                                           // averageMotion.y
        .addInt32(static_cast<int32_t>(snap.roomSize.x * 100.0f)) // scene width (cm)
        .addInt32(static_cast<int32_t>(snap.roomSize.y * 100.0f)); // scene height (cm)
    messages.push_back(scene.encode());

    const std::vector<uint8_t> bundle = osc::encodeBundle(messages);
    sender_.send(bundle.data(), bundle.size());
}

} // namespace sillage
