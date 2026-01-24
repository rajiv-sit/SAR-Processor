#pragma once

#include <cstdint>
#include <vector>

namespace sar {

struct SarTraceHeader {
    std::uint32_t syncWord = 0;
    std::uint16_t recordType = 0;
    std::uint16_t sceneNumber = 0;
    std::uint16_t recordNumber = 0;
    std::uint32_t timeStamp = 0;
    bool syncValid = true;
};

struct SarTraceRecord {
    SarTraceHeader header{};
    std::vector<std::int8_t> iqBytes;
};

}  // namespace sar
