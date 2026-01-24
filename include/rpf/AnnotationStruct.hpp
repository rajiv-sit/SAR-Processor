#pragma once

#include <cstdint>
#include <string>

namespace rpf {

struct ImageDataChunkHeader {
    std::uint32_t dataWidth = 0;
    std::uint32_t dataHeight = 0;
    std::int32_t frameSeqNum = 0;
    std::int32_t pixelType = 0;
    std::int32_t rspInhibit = 0;
    std::uint16_t pixelMarginStart = 0;
    std::uint16_t pixelMarginEnd = 0;
    std::uint16_t lineMarginStart = 0;
    std::uint16_t lineMarginEnd = 0;
};

struct FileIdParams {
    std::uint8_t radarMode = 0;
};

struct LatLongOutput {
    std::uint16_t geolocationGridNumLines = 0;
};

struct AnnotationStruct {
    FileIdParams fileIdParams{};
    LatLongOutput latLongOutput{};
    ImageDataChunkHeader imageDataChunkHeader{};
    std::string fileName;
};

}  // namespace rpf
