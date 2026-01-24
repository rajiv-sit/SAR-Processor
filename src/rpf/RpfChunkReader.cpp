#include "rpf/RpfChunkReader.hpp"

#include <array>

#include <cstring>
#include <Eigen/Core>

#include "rpf/RpfConstants.hpp"
#include "rpf/RpfImageDataParser.hpp"

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

}  // namespace

RpfChunkReader::RpfChunkReader(const std::string& path)
    : input_(path, std::ios::binary) {}

RpfChunkReader::~RpfChunkReader() = default;

bool RpfChunkReader::isOpen() const {
    return input_.is_open();
}

bool RpfChunkReader::readBlock(std::uint32_t blockIndex,
                               AnnotationStruct& annotation,
                               LatLongGrid& latLongGrid,
                               bool skipImageData) {
    if (!input_.is_open()) {
        return false;
    }

    std::uint32_t blockCount = 1;
    while (true) {
        RpfChunkHeader header{};
        if (!readChunkHeader(header)) {
            return false;
        }

        if (header.syncCode != RpfConstants::kChunkSyncCode) {
            return false;
        }

        if (header.chunkType == RpfConstants::kEndOfFileChunkTag) {
            return false;
        }

        if (header.chunkType == RpfConstants::kAnnotationDataChunkTag) {
            ++blockCount;
        }

        if (header.chunkType != RpfConstants::kImageDataChunkTag || blockCount != blockIndex) {
            input_.seekg(static_cast<std::streamoff>(header.bofOffsetToNextChunk), std::ios::beg);
            continue;
        }

        std::uint32_t nextOffset = 0;
        if (!readImageDataChunkHeader(annotation.imageDataChunkHeader, nextOffset)) {
            return false;
        }

        Eigen::MatrixXf imageData;
        RpfImageDataParser parser;
        if (!parser.parseImageData(input_, annotation.imageDataChunkHeader, skipImageData, imageData)) {
            return false;
        }

        if (!readAnnotationChunk(annotation, nextOffset)) {
            return false;
        }

        if (!readGeoGridLines(annotation, latLongGrid)) {
            return false;
        }

        input_.seekg(static_cast<std::streamoff>(nextOffset), std::ios::beg);
        return true;
    }
}

bool RpfChunkReader::readChunkHeader(RpfChunkHeader& header) {
    std::uint16_t sync = 0;
    if (!readU16Be(input_, sync)) {
        return false;
    }
    header.syncCode = sync;
    if (!readU16Be(input_, header.chunkType)) {
        return false;
    }
    if (!readU32Be(input_, header.chunkSize)) {
        return false;
    }
    const auto position = static_cast<std::uint32_t>(input_.tellg());
    header.bofOffsetToNextChunk =
        position + header.chunkSize * RpfConstants::kChunkBlockSize;
    return true;
}

bool RpfChunkReader::readImageDataChunkHeader(ImageDataChunkHeader& header, std::uint32_t& nextOffset) {
    const auto headerStart = static_cast<std::uint32_t>(input_.tellg());
    if (!readI32Be(input_, header.frameSeqNum)) {
        return false;
    }
        std::int32_t dataWidth = 0;
        std::int32_t dataHeight = 0;
        if (!readI32Be(input_, dataWidth)) {
            return false;
        }
        if (!readI32Be(input_, dataHeight)) {
            return false;
        }
        header.dataWidth = static_cast<std::uint32_t>(dataWidth);
        header.dataHeight = static_cast<std::uint32_t>(dataHeight);
        if (!readI32Be(input_, header.pixelType)) {
            return false;
        }
    if (!readI32Be(input_, header.rspInhibit)) {
        return false;
    }
    if (!readU16Be(input_, header.pixelMarginStart) ||
        !readU16Be(input_, header.pixelMarginEnd) ||
        !readU16Be(input_, header.lineMarginStart) ||
        !readU16Be(input_, header.lineMarginEnd)) {
        return false;
    }
    const auto position = static_cast<std::uint32_t>(input_.tellg());
    const auto headerEnd = headerStart + RpfConstants::kImageChunkHeaderSize;
    if (position < headerEnd) {
        input_.seekg(static_cast<std::streamoff>(headerEnd), std::ios::beg);
    }
    nextOffset = headerEnd;
    return true;
}

bool RpfChunkReader::readAnnotationChunk(AnnotationStruct& annotation, std::uint32_t& nextOffset) {
    const auto annotationStart = static_cast<std::uint32_t>(input_.tellg());
    RpfChunkHeader header{};
    if (!readChunkHeader(header)) {
        return false;
    }
    if (header.syncCode != RpfConstants::kChunkSyncCode ||
        header.chunkType != RpfConstants::kAnnotationDataChunkTag) {
        return false;
    }
    input_.seekg(static_cast<std::streamoff>(annotationStart + RpfConstants::kAnnotationHeaderSize),
                 std::ios::beg);

    std::int32_t fileType = 0;
    std::int32_t radarMode = 0;
    if (!readI32Be(input_, fileType) || !readI32Be(input_, radarMode)) {
        return false;
    }
    annotation.fileIdParams.radarMode = static_cast<std::uint8_t>(radarMode);

    input_.seekg(static_cast<std::streamoff>(annotationStart + RpfConstants::kAnnotationHeaderSize +
                                             RpfConstants::kProcImgFileIdSize +
                                             RpfConstants::kImgDisplayParamSize +
                                             RpfConstants::kDataAcqInfoSize +
                                             RpfConstants::kSeaspotTargetSize +
                                             RpfConstants::kLandspotTargetSize +
                                             RpfConstants::kStripmapTargetSize +
                                             RpfConstants::kProcInParamSize),
                 std::ios::beg);

    const auto dataProcStart = static_cast<std::uint32_t>(input_.tellg());
    input_.seekg(static_cast<std::streamoff>(dataProcStart +
                                             RpfConstants::kDataProcOutputSize -
                                             RpfConstants::kDataProcOutputTailSize),
                 std::ios::beg);

    for (int i = 0; i < 8; ++i) {
        std::uint32_t hi = 0;
        std::uint32_t lo = 0;
        if (!readU32Be(input_, hi) || !readU32Be(input_, lo)) {
            return false;
        }
    }

    std::int32_t gridLines = 0;
    if (!readI32Be(input_, gridLines)) {
        return false;
    }
    annotation.latLongOutput.geolocationGridNumLines =
        static_cast<std::uint16_t>(gridLines);

    nextOffset = annotationStart +
                 RpfConstants::kAnnotationHeaderSize +
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

    return true;
}

bool RpfChunkReader::readGeoGridLines(const AnnotationStruct& annotation, LatLongGrid& grid) {
    const std::size_t entries = annotation.latLongOutput.geolocationGridNumLines;
    grid.lineNumber.assign(entries, 0);
    grid.beginGrSrRatio.assign(entries, 0.0);
    grid.midGrSrRatio.assign(entries, 0.0);
    grid.endGrSrRatio.assign(entries, 0.0);
    grid.beginLatitude.assign(entries, 0.0);
    grid.beginLongitude.assign(entries, 0.0);
    grid.midLatitude.assign(entries, 0.0);
    grid.midLongitude.assign(entries, 0.0);
    grid.endLatitude.assign(entries, 0.0);
    grid.endLongitude.assign(entries, 0.0);
    for (std::size_t i = 0; i < entries; ++i) {
        std::int32_t lineNumber = 0;
        if (!readI32Be(input_, lineNumber)) {
            return false;
        }
        grid.lineNumber[i] = lineNumber;

        std::uint32_t buf = 0;
        if (!readU32Be(input_, buf)) {
            return false;
        }
        std::memcpy(&grid.beginGrSrRatio[i], &buf, sizeof(float));
        if (!readU32Be(input_, buf)) {
            return false;
        }
        std::memcpy(&grid.midGrSrRatio[i], &buf, sizeof(float));
        if (!readU32Be(input_, buf)) {
            return false;
        }
        std::memcpy(&grid.endGrSrRatio[i], &buf, sizeof(float));

        for (int j = 0; j < 6; ++j) {
            std::uint32_t hi = 0;
            std::uint32_t lo = 0;
            if (!readU32Be(input_, hi) || !readU32Be(input_, lo)) {
                return false;
            }
            std::uint64_t raw = (static_cast<std::uint64_t>(hi) << 32) |
                                static_cast<std::uint64_t>(lo);
            double value = 0.0;
            std::memcpy(&value, &raw, sizeof(value));
            switch (j) {
                case 0:
                    grid.beginLatitude[i] = value;
                    break;
                case 1:
                    grid.beginLongitude[i] = value;
                    break;
                case 2:
                    grid.midLatitude[i] = value;
                    break;
                case 3:
                    grid.midLongitude[i] = value;
                    break;
                case 4:
                    grid.endLatitude[i] = value;
                    break;
                case 5:
                    grid.endLongitude[i] = value;
                    break;
            }
        }
    }

    return true;
}

}  // namespace rpf
