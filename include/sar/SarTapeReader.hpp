#pragma once

#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

#include "sar/ISarTapeReader.hpp"
#include "sar/SarSceneHeader.hpp"
#include "sar/SarTraceRecord.hpp"

namespace sar {

struct SarTargetPositionMessage {
    double targetRange = 0.0;
    std::uint32_t timeStamp = 0;
    std::string targetLong;
    std::string targetLat;
};

class SarTapeReader : public ISarTapeReader {
public:
    explicit SarTapeReader(const std::string& path);
    ~SarTapeReader();

    SarTapeReader(const SarTapeReader&) = delete;
    SarTapeReader& operator=(const SarTapeReader&) = delete;

    SarTapeReader(SarTapeReader&&) noexcept = default;
    SarTapeReader& operator=(SarTapeReader&&) noexcept = default;

    bool isOpen() const override;
    bool readRecord(SarTraceRecord& record) override;
    bool hasSceneHeader() const override;
    const SarSceneHeader& sceneHeader() const override;
    bool hasAccessoryRecord() const override;
    const std::vector<SarTargetPositionMessage>& targetPositionMessages() const override;

private:
    bool ensureAccessoryParsed();

    std::ifstream input_;
    bool hasSceneHeader_ = false;
    SarSceneHeader sceneHeader_{};
    bool accessoryParsed_ = false;
    bool hasAccessoryRecord_ = false;
    std::vector<SarTargetPositionMessage> targetPositionMessages_{};
};

}  // namespace sar
