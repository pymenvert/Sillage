#pragma once

#include <chrono>
#include <cmath>
#include <cstdint>
#include <vector>

namespace sillage {

using Clock = std::chrono::steady_clock;
using TimePoint = Clock::time_point;
using Duration = Clock::duration;

struct Vec2 {
    float x = 0.0f;
    float y = 0.0f;

    Vec2 operator+(Vec2 o) const { return {x + o.x, y + o.y}; }
    Vec2 operator-(Vec2 o) const { return {x - o.x, y - o.y}; }
    Vec2 operator*(float s) const { return {x * s, y * s}; }
    float dot(Vec2 o) const { return x * o.x + y * o.y; }
    float normSq() const { return x * x + y * y; }
    float norm() const { return std::sqrt(normSq()); }
};

using SensorId = uint32_t;

// One point of a 2D lidar scan, in the sensor's polar frame.
struct RangePoint {
    float angle = 0.0f;    // radians, sensor frame
    float distance = 0.0f; // meters; <= 0 means invalid / no return
};

// A complete revolution (or sweep) from one sensor.
struct ScanFrame {
    SensorId sensor = 0;
    TimePoint captureTime{};
    std::vector<RangePoint> points;
};

// 2D pose of a sensor in the room frame.
struct SensorPose {
    Vec2 position{};
    float theta = 0.0f; // radians

    Vec2 toRoom(RangePoint p) const {
        const float a = theta + p.angle;
        return {position.x + p.distance * std::cos(a), position.y + p.distance * std::sin(a)};
    }
};

// A point in the room frame, annotated with its origin sensor.
struct WorldPoint {
    Vec2 pos{};
    SensorId sensor = 0;
};

struct Cluster {
    Vec2 centroid{};
    float radius = 0.0f; // RMS spread, meters
    uint32_t pointCount = 0;
    std::vector<uint32_t> members; // indices into the foreground point array
    // Part manufactured by prediction-seeded splitting: usable as measurement
    // for existing tracks, but must never spawn a new track (ghost births).
    bool fromPredictionSplit = false;
};

enum class TrackState : uint8_t {
    Tentative, // in probation, not published
    Confirmed,
    Coasting, // no measurement, running on prediction
};

struct Track {
    uint32_t id = 0;  // stable, never reused within a session
    uint32_t oid = 0; // compact ordered index among live confirmed tracks
    uint64_t bornTick = 0;
    Vec2 position{};
    Vec2 velocity{};
    float radius = 0.0f;
    TrackState state = TrackState::Tentative;
    float confidence = 0.0f;
};

// Immutable output of one pipeline tick, shared with output/UI threads.
struct FrameSnapshot {
    uint64_t tick = 0;
    double timeSeconds = 0.0;
    Vec2 roomSize{};
    std::vector<WorldPoint> foreground;
    std::vector<Cluster> clusters;
    std::vector<Track> tracks; // confirmed + coasting only (published set)
};

} // namespace sillage
