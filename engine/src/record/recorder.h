#pragma once

#include "core/types.h"

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace sillage {

// Raw scan recording and replay (.srec v1).
//
// Interim native container until the MCAP dependency lands with vcpkg
// (ADR-005): little-endian binary, append-only, one record per (tick, frame).
// Deterministic replay: feeding a recording through the pipeline reproduces
// the exact same tracks — a field incident becomes a test case.
//
// Layout: "SREC" u32-version, then records:
//   u64 tick | u32 sensor | u32 pointCount | pointCount x (f32 angle, f32 dist)

class ScanRecorder {
public:
    ~ScanRecorder() { close(); }

    bool open(const std::filesystem::path& file);
    void write(uint64_t tick, const ScanFrame& frame);
    void close();
    bool isOpen() const { return file_ != nullptr; }
    bool failed() const { return failed_; } // a write error occurred (disk full)

private:
    std::FILE* file_ = nullptr;
    bool failed_ = false;
};

class ScanReplayer {
public:
    ~ScanReplayer() { close(); }

    bool open(const std::filesystem::path& file);
    void close();

    // All frames recorded for the next tick, with the recorded tick number.
    // nullopt at end of file.
    std::optional<std::pair<uint64_t, std::vector<ScanFrame>>> nextTick();

    // Timeline-faithful replay: the frames due at engine tick `engineTick`,
    // where recorded ticks are rebased so the first record plays at tick 0.
    // The recorder only writes ticks that had frames, so a live session with
    // a 15 Hz sensor against a 60 Hz tick leaves 3-tick gaps — popping one
    // record group per engine tick (nextTick) compresses that timeline 4x,
    // and the replayed tracker sees velocities the live one never saw. An
    // empty vector means "nothing was recorded for this tick", exactly like
    // the live session; nullopt means the recording is over.
    std::optional<std::vector<ScanFrame>> nextTickAt(uint64_t engineTick);

private:
    std::optional<std::pair<uint64_t, ScanFrame>> readRecord();

    std::FILE* file_ = nullptr;
    std::optional<std::pair<uint64_t, ScanFrame>> pending_;
    std::optional<uint64_t> firstTick_; // rebases the recorded timeline to 0
};

} // namespace sillage
