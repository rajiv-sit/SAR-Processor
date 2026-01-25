#include "rpf/RpfWriter.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>

#include "rpf/RpfConstants.hpp"

namespace rpf {

namespace {

bool setError(std::string& error, const char* message) {
    error = message;
    return false;
}

void writeU16Be(std::ofstream& output, std::uint16_t value) {
    const std::uint8_t buf[2] = {
        static_cast<std::uint8_t>((value >> 8) & 0xFF),
        static_cast<std::uint8_t>(value & 0xFF)
    };
    output.write(reinterpret_cast<const char*>(buf), 2);
}

void writeU32Be(std::ofstream& output, std::uint32_t value) {
    const std::uint8_t buf[4] = {
        static_cast<std::uint8_t>((value >> 24) & 0xFF),
        static_cast<std::uint8_t>((value >> 16) & 0xFF),
        static_cast<std::uint8_t>((value >> 8) & 0xFF),
        static_cast<std::uint8_t>(value & 0xFF)
    };
    output.write(reinterpret_cast<const char*>(buf), 4);
}

void writeI32Be(std::ofstream& output, std::int32_t value) {
    writeU32Be(output, static_cast<std::uint32_t>(value));
}

std::uint16_t floatToHalf(float value) {
    if (std::isnan(value)) {
        return 0x7FFF;
    }
    if (std::isinf(value)) {
        return value < 0.0f ? 0xFC00 : 0x7C00;
    }
    const float clamped = value;
    const std::uint32_t bits = *reinterpret_cast<const std::uint32_t*>(&clamped);
    const std::uint32_t sign = (bits >> 16) & 0x8000;
    const std::int32_t exp = static_cast<std::int32_t>((bits >> 23) & 0xFF) - 127 + 15;
    const std::uint32_t mantissa = bits & 0x7FFFFF;

    if (exp <= 0) {
        if (exp < -10) {
            return static_cast<std::uint16_t>(sign);
        }
        const std::uint32_t shift = static_cast<std::uint32_t>(14 - exp);
        const std::uint32_t rounded = (mantissa | 0x800000) >> shift;
        return static_cast<std::uint16_t>(sign | rounded);
    }
    if (exp >= 31) {
        return static_cast<std::uint16_t>(sign | 0x7C00);
    }

    const std::uint16_t half = static_cast<std::uint16_t>(sign |
                                                          (static_cast<std::uint32_t>(exp) << 10) |
                                                          (mantissa >> 13));
    return half;
}

void writeF32Be(std::ofstream& output, float value) {
    std::uint32_t raw = 0;
    std::memcpy(&raw, &value, sizeof(raw));
    writeU32Be(output, raw);
}

void writeF64Be(std::ofstream& output, double value) {
    std::uint64_t raw = 0;
    std::memcpy(&raw, &value, sizeof(raw));
    const std::uint8_t buf[8] = {
        static_cast<std::uint8_t>((raw >> 56) & 0xFF),
        static_cast<std::uint8_t>((raw >> 48) & 0xFF),
        static_cast<std::uint8_t>((raw >> 40) & 0xFF),
        static_cast<std::uint8_t>((raw >> 32) & 0xFF),
        static_cast<std::uint8_t>((raw >> 24) & 0xFF),
        static_cast<std::uint8_t>((raw >> 16) & 0xFF),
        static_cast<std::uint8_t>((raw >> 8) & 0xFF),
        static_cast<std::uint8_t>(raw & 0xFF)
    };
    output.write(reinterpret_cast<const char*>(buf), 8);
}

void writeUtcDateTime(std::ofstream& output, const rpf::UtcDateTime& utc) {
    const std::uint16_t yearMonth =
        static_cast<std::uint16_t>(((utc.year & 0x0FFF) << 4) | (utc.month & 0x000F));
    const std::uint16_t dayHourMin =
        static_cast<std::uint16_t>(((utc.day & 0x1F) << 11) |
                                   ((utc.hour & 0x1F) << 6) |
                                   (utc.minute & 0x3F));
    const std::uint16_t secMsec =
        static_cast<std::uint16_t>(((utc.second & 0x3F) << 10) |
                                   (utc.millisec & 0x03FF));
    writeU16Be(output, yearMonth);
    writeU16Be(output, dayHourMin);
    writeU16Be(output, secMsec);
    writeU16Be(output, 0);
}

void writeZeros(std::ofstream& output, std::size_t count) {
    static constexpr std::size_t kChunk = 256;
    const std::uint8_t zero[kChunk] = {};
    while (count > 0) {
        const std::size_t step = std::min(count, kChunk);
        output.write(reinterpret_cast<const char*>(zero), static_cast<std::streamsize>(step));
        count -= step;
    }
}

void writeFixedString(std::ofstream& output, const std::string& value, std::size_t maxBytes) {
    const std::size_t count = std::min(value.size(), maxBytes);
    if (count > 0) {
        output.write(value.data(), static_cast<std::streamsize>(count));
    }
    if (maxBytes > count) {
        writeZeros(output, maxBytes - count);
    }
}

std::size_t alignTo8(std::size_t size) {
    return (size + 7u) & ~static_cast<std::size_t>(7u);
}

std::size_t bytesPerPixel(std::int32_t pixelType) {
    switch (pixelType) {
        case 0:
        case 1:
            return 1;
        case 4:
            return 2;
        case 5:
            return 8;
        case 6:
            return 4;
        default:
            return 4;
    }
}

bool writeChunkHeader(std::ofstream& output, std::uint16_t chunkType, std::uint32_t bodySize) {
    writeU16Be(output, RpfConstants::kChunkSyncCode);
    writeU16Be(output, chunkType);
    const std::uint32_t chunkBlocks = bodySize / RpfConstants::kChunkBlockSize;
    writeU32Be(output, chunkBlocks);
    return static_cast<bool>(output);
}

std::streampos writeChunkHeaderPlaceholder(std::ofstream& output, std::uint16_t chunkType) {
    const std::streampos start = output.tellp();
    writeU16Be(output, RpfConstants::kChunkSyncCode);
    writeU16Be(output, chunkType);
    writeU32Be(output, 0);
    return start;
}

bool finalizeChunkHeader(std::ofstream& output,
                         std::streampos start,
                         std::streampos end) {
    const auto bodyStart = start + static_cast<std::streamoff>(RpfConstants::kChunkCommonHeaderSize);
    const auto bodySize = static_cast<std::size_t>(end - bodyStart);
    const std::size_t aligned = alignTo8(bodySize);
    if (aligned > bodySize) {
        writeZeros(output, aligned - bodySize);
    }
    const std::uint32_t chunkBlocks = static_cast<std::uint32_t>(aligned / RpfConstants::kChunkBlockSize);
    output.seekp(start + static_cast<std::streamoff>(4), std::ios::beg);
    writeU32Be(output, chunkBlocks);
    output.seekp(0, std::ios::end);
    return static_cast<bool>(output);
}

}  // namespace

bool writeRpfFile(const std::string& path,
                  const Eigen::MatrixXf& image,
                  const RpfWriteOptions& options,
                  std::string& error) {
    const LatLongGrid emptyGrid{};
    return writeRpfFile(path, image, options, emptyGrid, error);
}

bool writeRpfFile(const std::string& path,
                  const Eigen::MatrixXf& image,
                  const RpfWriteOptions& options,
                  const LatLongGrid& grid,
                  std::string& error) {
    if (image.rows() <= 0 || image.cols() <= 0) {
        return setError(error, "RPF writer requires a non-empty image.");
    }
    if (options.geolocationGridNumLines < 2 || options.geolocationGridNumLines > 5) {
        return setError(error, "RPF writer requires 2-5 geolocation grid lines.");
    }
    if (!grid.lineNumber.empty() &&
        grid.lineNumber.size() != static_cast<std::size_t>(options.geolocationGridNumLines)) {
        return setError(error, "RPF writer grid size does not match geolocation grid count.");
    }

    std::ofstream output(path, std::ios::binary);
    if (!output) {
        return setError(error, "Failed to open RPF output file.");
    }

    const std::uint32_t width = static_cast<std::uint32_t>(image.cols());
    const std::uint32_t height = static_cast<std::uint32_t>(image.rows());
    const auto imageChunkStart = writeChunkHeaderPlaceholder(output, RpfConstants::kImageDataChunkTag);
    if (!output) return setError(error, "Failed to write image chunk header.");

    writeI32Be(output, options.frameSeqNum);
    writeI32Be(output, static_cast<std::int32_t>(width));
    writeI32Be(output, static_cast<std::int32_t>(height));
    writeI32Be(output, options.pixelType);
    writeI32Be(output, options.rspInhibit);
    writeU16Be(output, options.pixelMarginStart);
    writeU16Be(output, options.pixelMarginEnd);
    writeU16Be(output, options.lineMarginStart);
    writeU16Be(output, options.lineMarginEnd);
    writeZeros(output, RpfConstants::kImageChunkHeaderSize - 28);

    if (!output) return setError(error, "Failed to write image chunk header fields.");

    if (options.pixelType == 0) {
        for (std::int32_t row = 0; row < image.rows(); ++row) {
            for (std::int32_t col = 0; col < image.cols(); ++col) {
                const float value = std::clamp(image(row, col), 0.0f, 255.0f);
                const std::uint8_t byte = static_cast<std::uint8_t>(std::lround(value));
                output.write(reinterpret_cast<const char*>(&byte), 1);
            }
        }
    } else if (options.pixelType == 1) {
        for (std::int32_t row = 0; row < image.rows(); ++row) {
            for (std::int32_t col = 0; col < image.cols(); ++col) {
                const double value = std::max(0.0, std::min(65535.0,
                                                           static_cast<double>(image(row, col))));
                writeU16Be(output, static_cast<std::uint16_t>(std::lround(value)));
            }
        }
    } else if (options.pixelType == 2) {
        for (std::int32_t row = 0; row < image.rows(); ++row) {
            for (std::int32_t col = 0; col < image.cols(); ++col) {
                const double value = std::max(0.0, std::min(4294967295.0,
                                                           static_cast<double>(image(row, col))));
                writeU32Be(output, static_cast<std::uint32_t>(std::llround(value)));
            }
        }
    } else if (options.pixelType == 4) {
        for (std::int32_t row = 0; row < image.rows(); ++row) {
            for (std::int32_t col = 0; col < image.cols(); ++col) {
                const std::uint16_t half = floatToHalf(image(row, col));
                writeU16Be(output, half);
            }
        }
    } else if (options.pixelType == 5) {
        for (std::int32_t row = 0; row < image.rows(); ++row) {
            for (std::int32_t col = 0; col < image.cols(); ++col) {
                const float value = image(row, col);
                writeF32Be(output, value);
                writeF32Be(output, 0.0f);
            }
        }
    } else if (options.pixelType == 6) {
        for (std::int32_t row = 0; row < image.rows(); ++row) {
            for (std::int32_t col = 0; col < image.cols(); ++col) {
                const std::uint16_t half = floatToHalf(image(row, col));
                writeU16Be(output, half);
                writeU16Be(output, 0);
            }
        }
    } else {
        for (std::int32_t row = 0; row < image.rows(); ++row) {
            for (std::int32_t col = 0; col < image.cols(); ++col) {
                writeF32Be(output, image(row, col));
            }
        }
    }

    if (!output) return setError(error, "Failed to write image data.");
    const auto imageChunkEnd = output.tellp();
    if (!finalizeChunkHeader(output, imageChunkStart, imageChunkEnd)) return setError(error, "Failed to finalize image chunk header.");

    const std::size_t fixedAnnotationBytes =
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

    const auto annotationChunkStart = writeChunkHeaderPlaceholder(output, RpfConstants::kAnnotationDataChunkTag);
    if (!output) return setError(error, "Failed to write annotation chunk header.");

    std::size_t written = 0;
    writeZeros(output, RpfConstants::kAnnotationHeaderSize);
    written += RpfConstants::kAnnotationHeaderSize;

    writeI32Be(output, options.fileType);
    writeI32Be(output, options.radarMode);
    writeFixedString(output, options.formatVersion, 16);
    writeFixedString(output, options.fileId, RpfConstants::kProcImgFileIdSize - 8 - 16);
    written += RpfConstants::kProcImgFileIdSize;

    if (options.imgDisplayParams.bytes.size() == RpfConstants::kImgDisplayParamSize) {
        output.write(reinterpret_cast<const char*>(options.imgDisplayParams.bytes.data()),
                     static_cast<std::streamsize>(options.imgDisplayParams.bytes.size()));
    } else {
        writeZeros(output, RpfConstants::kImgDisplayParamSize);
    }
    written += RpfConstants::kImgDisplayParamSize;
    writeFixedString(output, options.dataAcquisition.aircraftId, 6);
    writeZeros(output, 2);
    writeFixedString(output, options.dataAcquisition.sortieNumber, 8);
    writeUtcDateTime(output, options.dataAcquisition.currentMissionStartTime);
    writeUtcDateTime(output, options.dataAcquisition.rawDataMissionStartTime);
    writeI32Be(output, options.dataAcquisition.currentAcqId);
    writeI32Be(output, options.dataAcquisition.rawDataAcqId);
    writeUtcDateTime(output, options.dataAcquisition.currentAcqStartTime);
    writeUtcDateTime(output, options.dataAcquisition.rawDataAcqStartTime);
    writeU32Be(output, options.dataAcquisition.resolution);
    writeU32Be(output, options.dataAcquisition.polarization);
    writeFixedString(output, options.dataAcquisition.hddrFileName, 40);
    writeI32Be(output, static_cast<std::int32_t>(options.startLine));
    writeI32Be(output, static_cast<std::int32_t>(options.startPixel));
    writeI32Be(output, static_cast<std::int32_t>(height));
    writeI32Be(output, static_cast<std::int32_t>(width));
    writeZeros(output, 88 - 16);
    written += RpfConstants::kDataAcqInfoSize;
    writeU32Be(output, options.seaspotTarget.tgtSelect);
    writeI32Be(output, options.seaspotTarget.trackId);
    writeI32Be(output, options.seaspotTarget.useCounter);
    writeU32Be(output, options.seaspotTarget.tgtVelocity);
    writeF64Be(output, options.seaspotTarget.tgtLatitude);
    writeF64Be(output, options.seaspotTarget.tgtLongitude);
    writeF32Be(output, options.seaspotTarget.tgtSpeed);
    writeF32Be(output, options.seaspotTarget.tgtCourse);
    writeF32Be(output, options.seaspotTarget.tgtElevation);
    writeZeros(output, 4);
    written += RpfConstants::kSeaspotTargetSize;
    writeU32Be(output, options.landspotTarget.tgtSelect);
    writeU32Be(output, options.landspotTarget.trackId);
    writeU32Be(output, options.landspotTarget.useCounter);
    writeF32Be(output, options.landspotTarget.tgtElevation);
    writeF64Be(output, options.landspotTarget.tgtLatitude);
    writeF64Be(output, options.landspotTarget.tgtLongitude);
    written += RpfConstants::kLandspotTargetSize;
    writeU32Be(output, options.stripmapTarget.tgtSelect);
    writeF32Be(output, options.stripmapTarget.tgtElevation);
    writeF64Be(output, options.stripmapTarget.tgtLatitude);
    writeF64Be(output, options.stripmapTarget.tgtLongitude);
    writeF64Be(output, options.stripmapTarget.tgt2Latitude);
    writeF64Be(output, options.stripmapTarget.tgt2Longitude);
    written += RpfConstants::kStripmapTargetSize;
    if (options.procInParams.bytes.size() == RpfConstants::kProcInParamSize) {
        output.write(reinterpret_cast<const char*>(options.procInParams.bytes.data()),
                     static_cast<std::streamsize>(options.procInParams.bytes.size()));
    } else {
        writeZeros(output, RpfConstants::kProcInParamSize);
    }
    written += RpfConstants::kProcInParamSize;

    if (options.dataProcOutput.bytes.size() == RpfConstants::kDataProcOutputSize) {
        output.write(reinterpret_cast<const char*>(options.dataProcOutput.bytes.data()),
                     static_cast<std::streamsize>(options.dataProcOutput.bytes.size()));
    } else {
        writeZeros(output, RpfConstants::kDataProcOutputSize - RpfConstants::kDataProcOutputTailSize);
        for (int i = 0; i < 8; ++i) {
            writeU32Be(output, 0);
            writeU32Be(output, 0);
        }
        writeI32Be(output, options.geolocationGridNumLines);
        writeZeros(output, RpfConstants::kDataProcOutputTailSize - 64 - 4);
    }
    written += RpfConstants::kDataProcOutputSize;

    writeF32Be(output, options.ownAircraftInfo.acHeading);
    writeF32Be(output, options.ownAircraftInfo.acSpeed);
    writeF64Be(output, options.ownAircraftInfo.acLatitude);
    writeF64Be(output, options.ownAircraftInfo.acLongitude);
    writeF64Be(output, options.ownAircraftInfo.acAltitude);
    writeZeros(output, 32);
    written += RpfConstants::kOwnAircraftInfoSize;

    if (options.procIdParams.bytes.size() == RpfConstants::kProcIdParamSize) {
        output.write(reinterpret_cast<const char*>(options.procIdParams.bytes.data()),
                     static_cast<std::streamsize>(options.procIdParams.bytes.size()));
    } else {
        writeZeros(output, RpfConstants::kProcIdParamSize);
    }
    written += RpfConstants::kProcIdParamSize;

    if (written > fixedAnnotationBytes) return setError(error, "RPF writer overflowed annotation block.");
    if (fixedAnnotationBytes > written) {
        writeZeros(output, fixedAnnotationBytes - written);
        written = fixedAnnotationBytes;
    }

    const std::uint16_t gridLines = options.geolocationGridNumLines;
    for (std::uint16_t i = 0; i < gridLines; ++i) {
        std::int32_t lineNumber =
            static_cast<std::int32_t>(options.startLine) +
            static_cast<std::int32_t>(i) *
                std::max<std::int32_t>(1, height / gridLines);
        float beginRatio = 1.0f;
        float midRatio = 1.0f;
        float endRatio = 1.0f;
        double beginLat = static_cast<double>(i);
        double beginLon = static_cast<double>(i);
        double midLat = beginLat + 0.5;
        double midLon = beginLon + 0.5;
        double endLat = beginLat + 1.0;
        double endLon = beginLon + 1.0;

        if (!grid.lineNumber.empty()) {
            lineNumber = grid.lineNumber[i];
            beginRatio = static_cast<float>(grid.beginGrSrRatio[i]);
            midRatio = static_cast<float>(grid.midGrSrRatio[i]);
            endRatio = static_cast<float>(grid.endGrSrRatio[i]);
            beginLat = grid.beginLatitude[i];
            beginLon = grid.beginLongitude[i];
            midLat = grid.midLatitude[i];
            midLon = grid.midLongitude[i];
            endLat = grid.endLatitude[i];
            endLon = grid.endLongitude[i];
        }

        writeI32Be(output, lineNumber);
        writeF32Be(output, beginRatio);
        writeF32Be(output, midRatio);
        writeF32Be(output, endRatio);
        writeF64Be(output, beginLat);
        writeF64Be(output, beginLon);
        writeF64Be(output, midLat);
        writeF64Be(output, midLon);
        writeF64Be(output, endLat);
        writeF64Be(output, endLon);
        written += 64u;
    }

    const auto annotationChunkEnd = output.tellp();
    if (!finalizeChunkHeader(output, annotationChunkStart, annotationChunkEnd)) return setError(error, "Failed to finalize annotation chunk header.");

    if (!writeChunkHeader(output, RpfConstants::kEndOfFileChunkTag, 0)) return setError(error, "Failed to write end-of-file chunk.");

    if (!output) return setError(error, "RPF writer failed while writing data.");

    return true;
}

}  // namespace rpf
