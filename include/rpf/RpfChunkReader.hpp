#pragma once

#include <cstdint>
#include <fstream>
#include <string>

#include "rpf/AnnotationStruct.hpp"
#include "rpf/LatLongGrid.hpp"

namespace rpf {

struct RpfChunkHeader {
    std::uint32_t syncCode = 0;
    std::uint16_t chunkType = 0;
    std::uint32_t chunkSize = 0;
    std::uint32_t bofOffsetToNextChunk = 0;
};

class RpfChunkReader {
public:
    explicit RpfChunkReader(const std::string& path);
    ~RpfChunkReader();

    RpfChunkReader(const RpfChunkReader&) = delete;
    RpfChunkReader& operator=(const RpfChunkReader&) = delete;
    RpfChunkReader(RpfChunkReader&&) noexcept = default;
    RpfChunkReader& operator=(RpfChunkReader&&) noexcept = default;

    bool isOpen() const;
    bool readBlock(std::uint32_t blockIndex,
                   AnnotationStruct& annotation,
                   LatLongGrid& latLongGrid,
                   bool skipImageData);

private:
    bool readChunkHeader(RpfChunkHeader& header);
    bool readImageDataChunkHeader(ImageDataChunkHeader& header, std::uint32_t& nextOffset);
    bool readAnnotationChunk(AnnotationStruct& annotation, std::uint32_t& nextOffset);
    bool readGeoGridLines(const AnnotationStruct& annotation, LatLongGrid& grid);

    std::ifstream input_;
};

}  // namespace rpf
