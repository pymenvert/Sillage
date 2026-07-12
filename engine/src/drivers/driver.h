#pragma once

#include "core/types.h"

#include <optional>
#include <string>

namespace sillage {

struct SensorHealth {
    bool connected = false;
    float scansPerSecond = 0.0f;
    uint64_t framesReceived = 0;
    uint64_t decodeErrors = 0;
    std::string lastError;
};

// Common contract for hardware sensor drivers. Each driver owns its thread(s)
// and connection lifecycle (reconnect with backoff); the pipeline polls the
// latest complete frame once per tick.
class ISensorDriver {
public:
    virtual ~ISensorDriver() = default;
    virtual const char* type() const = 0;
    virtual void start() = 0;
    virtual void stop() = 0;
    // Returns a frame only if newer than the caller's last seen sequence.
    virtual std::optional<ScanFrame> latestFrame(uint64_t& lastSeenSeq) = 0;
    virtual SensorHealth health() const = 0;
};

} // namespace sillage
