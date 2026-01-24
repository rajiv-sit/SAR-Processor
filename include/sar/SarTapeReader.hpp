#pragma once

#include <cstdint>
#include <fstream>
#include <string>

#include "sar/SarSceneHeader.hpp"
#include "sar/SarTraceRecord.hpp"

namespace sar {

class SarTapeReader {
public:
    explicit SarTapeReader(const std::string& path);
    ~SarTapeReader();

    SarTapeReader(const SarTapeReader&) = delete;
    SarTapeReader& operator=(const SarTapeReader&) = delete;

    SarTapeReader(SarTapeReader&&) noexcept = default;
    SarTapeReader& operator=(SarTapeReader&&) noexcept = default;

    bool isOpen() const;
    bool readRecord(SarTraceRecord& record);
    bool hasSceneHeader() const;
    const SarSceneHeader& sceneHeader() const;

private:
    std::ifstream input_;
    bool hasSceneHeader_ = false;
    SarSceneHeader sceneHeader_{};
};

}  // namespace sar
