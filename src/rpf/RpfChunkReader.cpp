#include "rpf/RpfChunkReader.hpp"

#include <array>
#include <cstring>
#include <string>
#include <vector>
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
    if (!readBytes(input, buf.data(), buf.size())) return false;
    value = static_cast<std::uint16_t>((buf[0] << 8) | buf[1]);
    return true;
}

bool readU32Be(std::ifstream& input, std::uint32_t& value) {
    std::array<std::uint8_t, 4> buf{};
    if (!readBytes(input, buf.data(), buf.size())) return false;
    value = (static_cast<std::uint32_t>(buf[0]) << 24) |
            (static_cast<std::uint32_t>(buf[1]) << 16) |
            (static_cast<std::uint32_t>(buf[2]) << 8) |
            static_cast<std::uint32_t>(buf[3]);
    return true;
}

bool readI32Be(std::ifstream& input, std::int32_t& value) {
    std::uint32_t temp = 0;
    if (!readU32Be(input, temp)) return false;
    value = static_cast<std::int32_t>(temp);
    return true;
}

bool readF32Be(std::ifstream& input, float& value) {
    std::uint32_t raw = 0;
    if (!readU32Be(input, raw)) return false;
    std::memcpy(&value, &raw, sizeof(value));
    return true;
}

bool readF64Be(std::ifstream& input, double& value) {
    std::array<std::uint8_t, 8> buf{};
    if (!readBytes(input, buf.data(), buf.size())) return false;
    std::uint64_t raw = (static_cast<std::uint64_t>(buf[0]) << 56) |
                        (static_cast<std::uint64_t>(buf[1]) << 48) |
                        (static_cast<std::uint64_t>(buf[2]) << 40) |
                        (static_cast<std::uint64_t>(buf[3]) << 32) |
                        (static_cast<std::uint64_t>(buf[4]) << 24) |
                        (static_cast<std::uint64_t>(buf[5]) << 16) |
                        (static_cast<std::uint64_t>(buf[6]) << 8) |
                        static_cast<std::uint64_t>(buf[7]);
    std::memcpy(&value, &raw, sizeof(value));
    return true;
}

std::string readFixedString(std::ifstream& input, std::size_t size) {
    std::string value(size, '\0');
    input.read(value.data(), static_cast<std::streamsize>(size));
    if (!input) {
        return {};
    }
    const auto endPos = value.find('\0');
    if (endPos != std::string::npos) {
        value.resize(endPos);
    }
    return value;
}

rpf::UtcDateTime readUtcDateTime(std::ifstream& input) {
    rpf::UtcDateTime utc{};
    std::uint16_t yearMonth = 0;
    std::uint16_t dayHourMin = 0;
    std::uint16_t secMsec = 0;
    std::uint16_t reserved = 0;
    if (!readU16Be(input, yearMonth) ||
        !readU16Be(input, dayHourMin) ||
        !readU16Be(input, secMsec) ||
        !readU16Be(input, reserved)) {
        return utc;
    }
    utc.year = static_cast<std::uint16_t>((yearMonth >> 4) & 0x0FFF);
    utc.month = static_cast<std::uint8_t>(yearMonth & 0x000F);
    utc.day = static_cast<std::uint8_t>((dayHourMin >> 11) & 0x1F);
    utc.hour = static_cast<std::uint8_t>((dayHourMin >> 6) & 0x1F);
    utc.minute = static_cast<std::uint8_t>(dayHourMin & 0x3F);
    utc.second = static_cast<std::uint8_t>((secMsec >> 10) & 0x3F);
    utc.millisec = static_cast<std::uint16_t>(secMsec & 0x03FF);
    (void)reserved;
    return utc;
}

}  // namespace

RpfChunkReader::RpfChunkReader(const std::string& path)
    : input_(path, std::ios::binary),
      path_(path) {}

RpfChunkReader::~RpfChunkReader() = default;

bool RpfChunkReader::isOpen() const {
    return input_.is_open();
}

bool RpfChunkReader::readBlock(std::uint32_t blockIndex,
                               AnnotationStruct& annotation,
                               LatLongGrid& latLongGrid,
                               bool skipImageData) {
    if (!input_.is_open()) return false;

    std::uint32_t blockCount = 1;
    while (true) {
        RpfChunkHeader header{};
        if (!readChunkHeader(header)) return false;

        if (header.syncCode != RpfConstants::kChunkSyncCode) return false;

        if (header.chunkType == RpfConstants::kEndOfFileChunkTag) return false;

        if (header.chunkType == RpfConstants::kAnnotationDataChunkTag) {
            ++blockCount;
        }

        if (header.chunkType != RpfConstants::kImageDataChunkTag || blockCount != blockIndex) {
            input_.seekg(static_cast<std::streamoff>(header.bofOffsetToNextChunk), std::ios::beg);
            continue;
        }

        std::uint32_t nextOffset = 0;
        if (!readImageDataChunkHeader(annotation.imageDataChunkHeader, nextOffset)) return false;
        annotation.imageRect.startLine = 1;
        annotation.imageRect.startPixel = 1;
        annotation.imageRect.numLines = annotation.imageDataChunkHeader.dataHeight;
        annotation.imageRect.numPixels = annotation.imageDataChunkHeader.dataWidth;
        annotation.acquisition.frameSeqNum = annotation.imageDataChunkHeader.frameSeqNum;
        annotation.acquisition.pixelType = annotation.imageDataChunkHeader.pixelType;
        annotation.acquisition.rspInhibit = annotation.imageDataChunkHeader.rspInhibit;
        annotation.acquisition.pixelMarginStart = annotation.imageDataChunkHeader.pixelMarginStart;
        annotation.acquisition.pixelMarginEnd = annotation.imageDataChunkHeader.pixelMarginEnd;
        annotation.acquisition.lineMarginStart = annotation.imageDataChunkHeader.lineMarginStart;
        annotation.acquisition.lineMarginEnd = annotation.imageDataChunkHeader.lineMarginEnd;
        annotation.fileName = path_;

        Eigen::MatrixXf imageData;
        RpfImageDataParser parser;
        if (!parser.parseImageData(input_, annotation.imageDataChunkHeader, skipImageData, imageData)) return false;

        input_.seekg(static_cast<std::streamoff>(header.bofOffsetToNextChunk), std::ios::beg);
        if (!readAnnotationChunk(annotation, nextOffset)) return false;

        if (!readGeoGridLines(annotation, latLongGrid)) return false;

        return true;
    }
}

bool RpfChunkReader::readChunkHeader(RpfChunkHeader& header) {
    std::uint16_t sync = 0;
    if (!readU16Be(input_, sync)) return false;
    header.syncCode = sync;
    if (!readU16Be(input_, header.chunkType)) return false;
    if (!readU32Be(input_, header.chunkSize)) return false;
    const auto position = static_cast<std::uint32_t>(input_.tellg());
    header.bofOffsetToNextChunk =
        position + header.chunkSize * RpfConstants::kChunkBlockSize;
    return true;
}

bool RpfChunkReader::readImageDataChunkHeader(ImageDataChunkHeader& header, std::uint32_t& nextOffset) {
    const auto headerStart = static_cast<std::uint32_t>(input_.tellg());
    if (!readI32Be(input_, header.frameSeqNum)) return false;
    std::int32_t dataWidth = 0;
    std::int32_t dataHeight = 0;
    if (!readI32Be(input_, dataWidth)) return false;
    if (!readI32Be(input_, dataHeight)) return false;
    header.dataWidth = static_cast<std::uint32_t>(dataWidth);
    header.dataHeight = static_cast<std::uint32_t>(dataHeight);
    if (!readI32Be(input_, header.pixelType)) return false;
    if (!readI32Be(input_, header.rspInhibit)) return false;
    if (!readU16Be(input_, header.pixelMarginStart) || !readU16Be(input_, header.pixelMarginEnd) ||
        !readU16Be(input_, header.lineMarginStart) || !readU16Be(input_, header.lineMarginEnd)) return false;
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
    if (!readChunkHeader(header)) return false;
    if (header.syncCode != RpfConstants::kChunkSyncCode || header.chunkType != RpfConstants::kAnnotationDataChunkTag) return false;
    const auto annotationPayloadStart =
        annotationStart + RpfConstants::kChunkCommonHeaderSize;
    input_.seekg(static_cast<std::streamoff>(annotationPayloadStart + RpfConstants::kAnnotationHeaderSize),
                 std::ios::beg);

    std::int32_t fileType = 0;
    std::int32_t radarMode = 0;
    if (!readI32Be(input_, fileType) || !readI32Be(input_, radarMode)) return false;
    annotation.fileIdParams.radarMode = static_cast<std::uint8_t>(radarMode);
    annotation.fileIdParams.fileType = fileType;

    annotation.processedImageFileId.fileType = fileType;
    annotation.processedImageFileId.radarMode = radarMode;
    annotation.processedImageFileId.formatVersion = readFixedString(input_, 16);
    if (!input_) return false;
    std::string fileId = readFixedString(input_, 40);
    if (!input_) return false;
    if (!fileId.empty()) {
        annotation.fileName = fileId;
    }

    const auto imgDisplayOffset =
        annotationPayloadStart + RpfConstants::kAnnotationHeaderSize +
        RpfConstants::kProcImgFileIdSize;
    input_.seekg(static_cast<std::streamoff>(imgDisplayOffset), std::ios::beg);
    annotation.imgDisplayParams.bytes.resize(RpfConstants::kImgDisplayParamSize);
    input_.read(reinterpret_cast<char*>(annotation.imgDisplayParams.bytes.data()),
                static_cast<std::streamsize>(annotation.imgDisplayParams.bytes.size()));
    if (!input_) return false;

    const auto dataAcqOffset = imgDisplayOffset + RpfConstants::kImgDisplayParamSize;
    input_.seekg(static_cast<std::streamoff>(dataAcqOffset), std::ios::beg);
    std::int32_t startLine = 0;
    std::int32_t startPixel = 0;
    std::int32_t numLines = 0;
    std::int32_t numPixels = 0;
    if (!readI32Be(input_, startLine) || !readI32Be(input_, startPixel) ||
        !readI32Be(input_, numLines) || !readI32Be(input_, numPixels)) {
        return false;
    }
    if (startLine > 0 && startPixel > 0 && numLines > 0 && numPixels > 0 &&
        static_cast<std::uint32_t>(numLines) <= annotation.imageDataChunkHeader.dataHeight &&
        static_cast<std::uint32_t>(numPixels) <= annotation.imageDataChunkHeader.dataWidth) {
        annotation.imageRect.startLine = static_cast<std::uint32_t>(startLine);
        annotation.imageRect.startPixel = static_cast<std::uint32_t>(startPixel);
        annotation.imageRect.numLines = static_cast<std::uint32_t>(numLines);
        annotation.imageRect.numPixels = static_cast<std::uint32_t>(numPixels);
    }

    annotation.dataAcquisition.aircraftId = readFixedString(input_, 6);
    if (!input_) return false;
    std::uint8_t padding[2] = {};
    if (!readBytes(input_, padding, sizeof(padding))) return false;
    annotation.dataAcquisition.sortieNumber = readFixedString(input_, 8);
    if (!input_) return false;
    annotation.dataAcquisition.currentMissionStartTime = readUtcDateTime(input_);
    annotation.dataAcquisition.rawDataMissionStartTime = readUtcDateTime(input_);
    if (!readI32Be(input_, annotation.dataAcquisition.currentAcqId) ||
        !readI32Be(input_, annotation.dataAcquisition.rawDataAcqId)) {
        return false;
    }
    annotation.dataAcquisition.currentAcqStartTime = readUtcDateTime(input_);
    annotation.dataAcquisition.rawDataAcqStartTime = readUtcDateTime(input_);
    if (!readU32Be(input_, annotation.dataAcquisition.resolution) ||
        !readU32Be(input_, annotation.dataAcquisition.polarization)) {
        return false;
    }
    annotation.dataAcquisition.hddrFileName = readFixedString(input_, 40);
    if (!input_) return false;
    std::vector<std::uint8_t> dataAcqSpare(88);
    if (!readBytes(input_, dataAcqSpare.data(), dataAcqSpare.size())) return false;

    if (!readU32Be(input_, annotation.seaspotTarget.tgtSelect) ||
        !readI32Be(input_, annotation.seaspotTarget.trackId) ||
        !readI32Be(input_, annotation.seaspotTarget.useCounter) ||
        !readU32Be(input_, annotation.seaspotTarget.tgtVelocity) ||
        !readF64Be(input_, annotation.seaspotTarget.tgtLatitude) ||
        !readF64Be(input_, annotation.seaspotTarget.tgtLongitude) ||
        !readF32Be(input_, annotation.seaspotTarget.tgtSpeed) ||
        !readF32Be(input_, annotation.seaspotTarget.tgtCourse) ||
        !readF32Be(input_, annotation.seaspotTarget.tgtElevation)) {
        return false;
    }
    std::uint8_t seaReserved[4] = {};
    if (!readBytes(input_, seaReserved, sizeof(seaReserved))) return false;

    if (!readU32Be(input_, annotation.landspotTarget.tgtSelect) ||
        !readU32Be(input_, annotation.landspotTarget.trackId) ||
        !readU32Be(input_, annotation.landspotTarget.useCounter) ||
        !readF32Be(input_, annotation.landspotTarget.tgtElevation) ||
        !readF64Be(input_, annotation.landspotTarget.tgtLatitude) ||
        !readF64Be(input_, annotation.landspotTarget.tgtLongitude)) {
        return false;
    }

    if (!readU32Be(input_, annotation.stripmapTarget.tgtSelect) ||
        !readF32Be(input_, annotation.stripmapTarget.tgtElevation) ||
        !readF64Be(input_, annotation.stripmapTarget.tgtLatitude) ||
        !readF64Be(input_, annotation.stripmapTarget.tgtLongitude) ||
        !readF64Be(input_, annotation.stripmapTarget.tgt2Latitude) ||
        !readF64Be(input_, annotation.stripmapTarget.tgt2Longitude)) {
        return false;
    }

    input_.seekg(static_cast<std::streamoff>(annotationPayloadStart + RpfConstants::kAnnotationHeaderSize +
                                             RpfConstants::kProcImgFileIdSize +
                                             RpfConstants::kImgDisplayParamSize +
                                             RpfConstants::kDataAcqInfoSize +
                                             RpfConstants::kSeaspotTargetSize +
                                             RpfConstants::kLandspotTargetSize +
                                             RpfConstants::kStripmapTargetSize),
                 std::ios::beg);

    annotation.procInParams.bytes.resize(RpfConstants::kProcInParamSize);
    input_.read(reinterpret_cast<char*>(annotation.procInParams.bytes.data()),
                static_cast<std::streamsize>(annotation.procInParams.bytes.size()));
    if (!input_) return false;

    annotation.dataProcOutput.bytes.resize(RpfConstants::kDataProcOutputSize);
    input_.read(reinterpret_cast<char*>(annotation.dataProcOutput.bytes.data()),
                static_cast<std::streamsize>(annotation.dataProcOutput.bytes.size()));
    if (!input_) return false;

    const std::size_t tailOffset =
        RpfConstants::kDataProcOutputSize - RpfConstants::kDataProcOutputTailSize + 64u;
    if (annotation.dataProcOutput.bytes.size() >= tailOffset + 4u) {
        std::uint32_t gridLinesRaw = 0;
        std::memcpy(&gridLinesRaw,
                    annotation.dataProcOutput.bytes.data() + tailOffset,
                    sizeof(gridLinesRaw));
        gridLinesRaw = (gridLinesRaw >> 24) |
                       ((gridLinesRaw >> 8) & 0x0000FF00) |
                       ((gridLinesRaw << 8) & 0x00FF0000) |
                       (gridLinesRaw << 24);
        annotation.latLongOutput.geolocationGridNumLines =
            static_cast<std::uint16_t>(gridLinesRaw);
    }

    if (!readF32Be(input_, annotation.ownAircraftInfo.acHeading) ||
        !readF32Be(input_, annotation.ownAircraftInfo.acSpeed) ||
        !readF64Be(input_, annotation.ownAircraftInfo.acLatitude) ||
        !readF64Be(input_, annotation.ownAircraftInfo.acLongitude) ||
        !readF64Be(input_, annotation.ownAircraftInfo.acAltitude)) {
        return false;
    }
    std::vector<std::uint8_t> ownAircraftSpare(32);
    if (!readBytes(input_, ownAircraftSpare.data(), ownAircraftSpare.size())) return false;

    annotation.procIdParams.bytes.resize(RpfConstants::kProcIdParamSize);
    input_.read(reinterpret_cast<char*>(annotation.procIdParams.bytes.data()),
                static_cast<std::streamsize>(annotation.procIdParams.bytes.size()));
    if (!input_) return false;

    annotation.notes.summary =
        "fileType=" + std::to_string(fileType) +
        " radarMode=" + std::to_string(radarMode) +
        " gridLines=" + std::to_string(annotation.latLongOutput.geolocationGridNumLines);
    if (!fileId.empty()) {
        annotation.notes.summary += " fileId=" + fileId;
    }

    nextOffset = annotationPayloadStart +
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
        if (!readI32Be(input_, lineNumber)) return false;
        grid.lineNumber[i] = lineNumber;

        std::uint32_t buf = 0;
        if (!readU32Be(input_, buf)) return false;
        std::memcpy(&grid.beginGrSrRatio[i], &buf, sizeof(float));
        if (!readU32Be(input_, buf)) return false;
        std::memcpy(&grid.midGrSrRatio[i], &buf, sizeof(float));
        if (!readU32Be(input_, buf)) return false;
        std::memcpy(&grid.endGrSrRatio[i], &buf, sizeof(float));

        for (int j = 0; j < 6; ++j) {
            std::uint32_t hi = 0;
            std::uint32_t lo = 0;
            if (!readU32Be(input_, hi) || !readU32Be(input_, lo)) return false;
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
