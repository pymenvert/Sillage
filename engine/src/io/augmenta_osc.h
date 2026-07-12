#pragma once

#include "core/types.h"
#include "net/net.h"

#include <string>
#include <unordered_set>

namespace sillage {

// Augmenta legacy OSC protocol output (/au/personEntered|personUpdated|
// personWillLeave|scene). Field layout follows the published legacy spec;
// exact conformance is validated against the official plugins in M4
// (see docs/06-protocoles-et-integrations.md).
class AugmentaOscOutput {
public:
    bool open(const std::string& host, uint16_t port);
    void publish(const FrameSnapshot& snapshot);

private:
    net::UdpSender sender_;
    std::unordered_set<uint32_t> knownIds_;
};

} // namespace sillage
