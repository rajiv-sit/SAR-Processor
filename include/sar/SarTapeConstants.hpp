#pragma once

#include <cstddef>
#include <cstdint>

namespace sar {

struct SarTapeConstants {
    static constexpr std::uint32_t kSyncWord = 0x0EB22000;
    static constexpr std::size_t kRecordSize = 2048;
    static constexpr std::size_t kRecordHeaderSize = 28;
    static constexpr std::size_t kTestRampSize = 76;
    static constexpr std::uint16_t kRecordTypeDummy = 0;
    static constexpr std::uint16_t kRecordTypeSceneHeader = 1;
    static constexpr std::uint16_t kRecordTypeData = 2;
    static constexpr std::uint16_t kRecordTypeNull = 3;
    static constexpr std::uint16_t kRecordTypeEof = 15;
    static constexpr std::uint16_t kSarModeMaskMultiScene = 1;
    static constexpr std::uint16_t kSarModeMaskRdp = 16;
    static constexpr std::uint32_t kMaxVideoRecsPerScene = 32772;
    static constexpr std::uint32_t kMaxVideoRecsSpot = 12448;
    static constexpr std::uint16_t kMaxSceneHeaderSize = 122;
};

}  // namespace sar
