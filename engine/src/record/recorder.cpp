#include "record/recorder.h"

#include <cstring>

namespace sillage {

namespace {
constexpr char kMagic[4] = {'S', 'R', 'E', 'C'};
constexpr uint32_t kVersion = 1;
} // namespace

bool ScanRecorder::open(const std::filesystem::path& file) {
    close();
    file_ = std::fopen(file.string().c_str(), "wb");
    if (file_ == nullptr) {
        return false;
    }
    std::fwrite(kMagic, 1, 4, file_);
    std::fwrite(&kVersion, sizeof(kVersion), 1, file_);
    return true;
}

void ScanRecorder::write(uint64_t tick, const ScanFrame& frame) {
    if (file_ == nullptr) {
        return;
    }
    std::fwrite(&tick, sizeof(tick), 1, file_);
    std::fwrite(&frame.sensor, sizeof(frame.sensor), 1, file_);
    const auto count = static_cast<uint32_t>(frame.points.size());
    std::fwrite(&count, sizeof(count), 1, file_);
    static_assert(sizeof(RangePoint) == 8, "RangePoint must stay {f32,f32}");
    std::fwrite(frame.points.data(), sizeof(RangePoint), frame.points.size(), file_);
}

void ScanRecorder::close() {
    if (file_ != nullptr) {
        std::fclose(file_);
        file_ = nullptr;
    }
}

bool ScanReplayer::open(const std::filesystem::path& file) {
    close();
    file_ = std::fopen(file.string().c_str(), "rb");
    if (file_ == nullptr) {
        return false;
    }
    char magic[4];
    uint32_t version = 0;
    if (std::fread(magic, 1, 4, file_) != 4 || std::memcmp(magic, kMagic, 4) != 0 ||
        std::fread(&version, sizeof(version), 1, file_) != 1 || version != kVersion) {
        close();
        return false;
    }
    return true;
}

void ScanReplayer::close() {
    if (file_ != nullptr) {
        std::fclose(file_);
        file_ = nullptr;
    }
    pending_.reset();
}

std::optional<std::pair<uint64_t, ScanFrame>> ScanReplayer::readRecord() {
    if (file_ == nullptr) {
        return std::nullopt;
    }
    uint64_t tick = 0;
    ScanFrame frame;
    uint32_t count = 0;
    if (std::fread(&tick, sizeof(tick), 1, file_) != 1 ||
        std::fread(&frame.sensor, sizeof(frame.sensor), 1, file_) != 1 ||
        std::fread(&count, sizeof(count), 1, file_) != 1 || count > 1000000) {
        return std::nullopt;
    }
    frame.points.resize(count);
    if (std::fread(frame.points.data(), sizeof(RangePoint), count, file_) != count) {
        return std::nullopt;
    }
    return std::pair{tick, std::move(frame)};
}

std::optional<std::pair<uint64_t, std::vector<ScanFrame>>> ScanReplayer::nextTick() {
    if (!pending_) {
        pending_ = readRecord();
        if (!pending_) {
            return std::nullopt;
        }
    }
    const uint64_t tick = pending_->first;
    std::vector<ScanFrame> frames;
    while (pending_ && pending_->first == tick) {
        frames.push_back(std::move(pending_->second));
        pending_ = readRecord();
    }
    return std::pair{tick, std::move(frames)};
}

} // namespace sillage
