#include "rpf/RpfQuery.hpp"

#include <array>
#include <fstream>

#include "rpf/RpfConstants.hpp"

namespace rpf {

namespace {

bool readBytes(std::ifstream& input, std::uint8_t* buffer, std::size_t size) {
    input.read(reinterpret_cast<char*>(buffer), static_cast<std::streamsize>(size));
    return static_cast<bool>(input);
}

bool readU16Be(std::ifstream& input, std::uint16_t& value) {
    std::array<std::uint8_t, 2> buf{};
    if (!readBytes(input, buf.data(), buf.size())) {
        return false;
    }
    value = static_cast<std::uint16_t>((buf[0] << 8) | buf[1]);
    return true;
}

bool readU32Be(std::ifstream& input, std::uint32_t& value) {
    std::array<std::uint8_t, 4> buf{};
    if (!readBytes(input, buf.data(), buf.size())) {
        return false;
    }
    value = (static_cast<std::uint32_t>(buf[0]) << 24) |
            (static_cast<std::uint32_t>(buf[1]) << 16) |
            (static_cast<std::uint32_t>(buf[2]) << 8) |
            static_cast<std::uint32_t>(buf[3]);
    return true;
}

bool readI32Be(std::ifstream& input, std::int32_t& value) {
    std::uint32_t temp = 0;
    if (!readU32Be(input, temp)) {
        return false;
    }
    value = static_cast<std::int32_t>(temp);
    return true;
}

struct ChunkHeader {
    std::uint16_t syncCode = 0;
    std::uint16_t chunkType = 0;
    std::uint32_t chunkSize = 0;
    std::uint32_t bofOffsetToNextChunk = 0;
};

bool readChunkHeader(std::ifstream& input, ChunkHeader& header) {
    std::uint16_t sync = 0;
    if (!readU16Be(input, sync)) {
        return false;
    }
    header.syncCode = sync;
    if (!readU16Be(input, header.chunkType)) {
        return false;
    }
    if (!readU32Be(input, header.chunkSize)) {
        return false;
    }
    const auto position = static_cast<std::uint32_t>(input.tellg());
    header.bofOffsetToNextChunk =
        position + header.chunkSize * RpfConstants::kChunkBlockSize;
    return true;
}

}  // namespace

bool queryRpfFile(const std::string& fileName, RpfQueryResult& result) {
    std::ifstream input(fileName, std::ios::binary);
    if (!input) {
        return false;
    }

    while (true) {
        const auto chunkHeaderStart = static_cast<std::uint32_t>(input.tellg());
        ChunkHeader header{};
        if (!readChunkHeader(input, header)) {
            return false;
        }

        if (header.syncCode != RpfConstants::kChunkSyncCode) {
            return false;
        }

        if (header.chunkType == RpfConstants::kEndOfFileChunkTag) {
            break;
        }

        if (header.chunkType != RpfConstants::kImageDataChunkTag) {
            input.seekg(static_cast<std::streamoff>(header.bofOffsetToNextChunk), std::ios::beg);
            continue;
        }

        const std::uint32_t bofImgOffset = chunkHeaderStart + RpfConstants::kImageChunkHeaderSize;
        std::int32_t frameSeqNum = 0;
        std::int32_t dataWidth = 0;
        std::int32_t dataHeight = 0;
        std::int32_t pixelType = 0;
        std::int32_t rspInhibit = 0;
        std::uint16_t marginStart = 0;
        std::uint16_t marginEnd = 0;
        std::uint16_t lineMarginStart = 0;
        std::uint16_t lineMarginEnd = 0;

        if (!readI32Be(input, frameSeqNum) ||
            !readI32Be(input, dataWidth) ||
            !readI32Be(input, dataHeight) ||
            !readI32Be(input, pixelType) ||
            !readI32Be(input, rspInhibit) ||
            !readU16Be(input, marginStart) ||
            !readU16Be(input, marginEnd) ||
            !readU16Be(input, lineMarginStart) ||
            !readU16Be(input, lineMarginEnd)) {
            return false;
        }

        const auto imageHeaderEnd = chunkHeaderStart + RpfConstants::kImageChunkHeaderSize;
        if (static_cast<std::uint32_t>(input.tellg()) < imageHeaderEnd) {
            input.seekg(static_cast<std::streamoff>(imageHeaderEnd), std::ios::beg);
        }

        const auto annotationStart = static_cast<std::uint32_t>(input.tellg());
        ChunkHeader annotationHeader{};
        if (!readChunkHeader(input, annotationHeader)) {
            return false;
        }
        if (annotationHeader.syncCode != RpfConstants::kChunkSyncCode ||
            annotationHeader.chunkType != RpfConstants::kAnnotationDataChunkTag) {
            return false;
        }

        input.seekg(static_cast<std::streamoff>(annotationStart + RpfConstants::kAnnotationHeaderSize), std::ios::beg);
        std::int32_t fileType = 0;
        std::int32_t radarMode = 0;
        if (!readI32Be(input, fileType) || !readI32Be(input, radarMode)) {
            return false;
        }
        if (result.mode == -1) {
            result.mode = radarMode;
        }

        int startNum = 0;
        if (radarMode == RpfConstants::kStripmapMode) {
            const std::uint32_t geoGridOffset =
                annotationStart + RpfConstants::kAnnotationHeaderSize +
                RpfConstants::kProcImgFileIdSize +
                RpfConstants::kImgDisplayParamSize +
                RpfConstants::kDataAcqInfoSize +
                RpfConstants::kSeaspotTargetSize +
                RpfConstants::kLandspotTargetSize +
                RpfConstants::kStripmapTargetSize +
                RpfConstants::kProcInParamSize +
                RpfConstants::kDataProcOutputSize +
                RpfConstants::kOwnAircraftInfoSize +
                RpfConstants::kProcIdParamSize;

            input.seekg(static_cast<std::streamoff>(geoGridOffset), std::ios::beg);
            std::int32_t lineNumber = 0;
            if (!readI32Be(input, lineNumber)) {
                return false;
            }
            startNum = lineNumber;
        } else {
            startNum = frameSeqNum;
        }

        result.startNums.push_back(startNum);
        result.numLines.push_back(dataHeight);
        result.numPixels.push_back(dataWidth);
        result.pixelTypes.push_back(pixelType);
        result.bofImgOffsets.push_back(bofImgOffset);

        input.seekg(static_cast<std::streamoff>(annotationHeader.bofOffsetToNextChunk), std::ios::beg);
    }

    return true;
}

}  // namespace rpf
