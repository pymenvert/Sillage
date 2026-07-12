#pragma once

#include "config/project.h"
#include "core/types.h"

#include <map>
#include <string>
#include <vector>

namespace sillage {

struct ZoneEvent {
    enum class Type { Enter, Exit };
    Type type;
    std::string zone;
    uint32_t trackId;
};

// Polygon zones with enter/exit events, occupancy and entry counters
// (docs/02 `logic/`). Point-in-polygon by ray casting.
class ZoneEngine {
public:
    explicit ZoneEngine(std::vector<ZoneConfig> zones) : zones_(std::move(zones)) {
        inside_.resize(zones_.size());
        entries_.resize(zones_.size(), 0);
    }

    std::vector<ZoneEvent> update(const std::vector<Track>& tracks);

    struct ZoneStatus {
        const ZoneConfig* zone = nullptr;
        uint32_t occupants = 0;
        uint64_t totalEntries = 0;
    };
    std::vector<ZoneStatus> status() const;

    static bool pointInPolygon(Vec2 p, const std::vector<Vec2>& polygon);

private:
    std::vector<ZoneConfig> zones_;
    std::vector<std::map<uint32_t, bool>> inside_; // per zone: track id -> inside
    std::vector<uint64_t> entries_;
};

} // namespace sillage
