#pragma once

#include "core/json.h"
#include "core/types.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace sillage {

struct ZoneConfig {
    std::string name;
    std::vector<Vec2> polygon; // room frame, >= 3 vertices
};

struct SensorConfig {
    std::string type = "hokuyo"; // "hokuyo" (real) — sim sensors come from demo mode
    std::string host;
    uint16_t port = 10940;
    SensorPose pose{};
};

// The project file: everything an installation needs to come back after a
// reboot. Versioned; unknown keys are ignored (forward compatibility),
// missing keys take defaults (backward compatibility).
struct ProjectConfig {
    static constexpr int kVersion = 1;

    Vec2 roomSize{10.0f, 8.0f};
    bool simEnabled = true; // demo simulator (disabled on real installs)
    std::vector<SensorConfig> sensors;
    std::vector<ZoneConfig> zones;

    // Outputs.
    bool oscEnabled = true;
    std::string oscHost = "127.0.0.1";
    uint16_t oscPort = 12000;
    bool tuioEnabled = false;
    std::string tuioHost = "127.0.0.1";
    uint16_t tuioPort = 3333;
    bool admEnabled = false;
    std::string admHost = "127.0.0.1";
    uint16_t admPort = 4001;
    uint32_t admMaxObjects = 16;

    // Output conditioning (docs/03 §9).
    float predictionSeconds = 0.0f; // extrapolate published positions by this much
    bool smoothing = false;         // One-Euro filter on published positions

    json::Value toJson() const;
    static std::optional<ProjectConfig> fromJson(const json::Value& v, std::string& error);

    // Atomic save (write temp + rename) and load with explicit error text.
    bool save(const std::filesystem::path& file, std::string& error) const;
    static std::optional<ProjectConfig> load(const std::filesystem::path& file,
                                             std::string& error);
};

} // namespace sillage
