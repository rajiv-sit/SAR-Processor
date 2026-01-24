#pragma once

#include <cstdint>

namespace rpf {

struct RpfConstants {
    static constexpr std::uint16_t kChunkSyncCode = 0xDCDC;
    static constexpr std::uint16_t kImageDataChunkTag = 0;
    static constexpr std::uint16_t kAnnotationDataChunkTag = 200;
    static constexpr std::uint16_t kEndOfFileChunkTag = 32768;
    static constexpr std::uint32_t kChunkBlockSize = 8;
    static constexpr std::uint32_t kChunkCommonHeaderSize = 8;
    static constexpr std::uint32_t kImageChunkHeaderSize = 64;
    static constexpr std::uint32_t kAnnotationHeaderSize = 64;
    static constexpr std::uint32_t kProcImgFileIdSize = 64;
    static constexpr std::uint32_t kImgDisplayParamSize = 64;
    static constexpr std::uint32_t kDataAcqInfoSize = 192;
    static constexpr std::uint32_t kSeaspotTargetSize = 48;
    static constexpr std::uint32_t kLandspotTargetSize = 32;
    static constexpr std::uint32_t kStripmapTargetSize = 40;
    static constexpr std::uint32_t kProcInParamSize = 136;
    static constexpr std::uint32_t kDataProcOutputSize = 640;
    static constexpr std::uint32_t kOwnAircraftInfoSize = 64;
    static constexpr std::uint32_t kProcIdParamSize = 256;
    static constexpr std::uint32_t kGeoGridLineSize = 64;
    static constexpr std::uint32_t kDataProcOutputTailSize = 96;
};

}  // namespace rpf
