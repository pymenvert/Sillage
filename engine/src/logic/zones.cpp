#include "logic/zones.h"

namespace sillage {

bool ZoneEngine::pointInPolygon(Vec2 p, const std::vector<Vec2>& polygon) {
    bool inside = false;
    const size_t n = polygon.size();
    for (size_t i = 0, j = n - 1; i < n; j = i++) {
        const Vec2 a = polygon[i];
        const Vec2 b = polygon[j];
        if ((a.y > p.y) != (b.y > p.y) &&
            p.x < (b.x - a.x) * (p.y - a.y) / (b.y - a.y) + a.x) {
            inside = !inside;
        }
    }
    return inside;
}

std::vector<ZoneEvent> ZoneEngine::update(const std::vector<Track>& tracks) {
    std::vector<ZoneEvent> events;
    for (size_t z = 0; z < zones_.size(); ++z) {
        auto& state = inside_[z];
        // Mark everyone currently inside.
        std::map<uint32_t, bool> now;
        for (const Track& t : tracks) {
            if (pointInPolygon(t.position, zones_[z].polygon)) {
                now[t.id] = true;
            }
        }
        // Enters.
        for (const auto& [id, _] : now) {
            if (!state.contains(id)) {
                events.push_back({ZoneEvent::Type::Enter, zones_[z].name, id});
                ++entries_[z];
            }
        }
        // Exits (left the polygon or the track died).
        for (const auto& [id, _] : state) {
            if (!now.contains(id)) {
                events.push_back({ZoneEvent::Type::Exit, zones_[z].name, id});
            }
        }
        state = std::move(now);
    }
    return events;
}

std::vector<ZoneEngine::ZoneStatus> ZoneEngine::status() const {
    std::vector<ZoneStatus> out;
    out.reserve(zones_.size());
    for (size_t z = 0; z < zones_.size(); ++z) {
        out.push_back({&zones_[z], static_cast<uint32_t>(inside_[z].size()), entries_[z]});
    }
    return out;
}

} // namespace sillage
