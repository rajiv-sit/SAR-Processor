#include "rpf/RpfWriter.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>

#include "rpf/RpfConstants.hpp"

namespace rpf {

namespace {

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

void writeZeros(std::ofstream& output, std::size_t count) {
    static constexpr std::size_t kChunk = 256;
    const std::uint8_t zero[kChunk] = {};
    while (count > 0) {
        const std::size_t step = std::min(count, kChunk);
        output.write(reinterpret_cast<const char*>(zero), static_cast<std::streamsize>(step));
        count -= step;
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
    if (image.rows() <= 0 || image.cols() <= 0) {
        error = "RPF writer requires a non-empty image.";
        return false;
    }
    if (options.geolocationGridNumLines < 2 || options.geolocationGridNumLines > 5) {
        error = "RPF writer requires 2-5 geolocation grid lines.";
        return false;
    }

    std::ofstream output(path, std::ios::binary);
    if (!output) {
        error = "Failed to open RPF output file.";
        return false;
    }

    const std::uint32_t width = static_cast<std::uint32_t>(image.cols());
    const std::uint32_t height = static_cast<std::uint32_t>(image.rows());
    const auto imageChunkStart = writeChunkHeaderPlaceholder(output, RpfConstants::kImageDataChunkTag);
    if (!output) {
        error = "Failed to write image chunk header.";
        return false;
    }

    writeI32Be(output, options.frameSeqNum);
    writeI32Be(output, static_cast<std::int32_t>(width));
    writeI32Be(output, static_cast<std::int32_t>(height));
    writeI32Be(output, options.pixelType);
    writeI32Be(output, options.rspInhibit);
    writeU16Be(output, options.pixelMarginStart);
    writeU16Be(output, options.pixelMarginEnd);
    writeU16Be(output, options.lineMarginStart);
    writeU16Be(output, options.lineMarginEnd);
    writeZeros(output, RpfConstants::kImageChunkHeaderSize - 24);

    if (!output) {
        error = "Failed to write image chunk header fields.";
        return false;
    }

    if (options.pixelType == 0 || options.pixelType == 1) {
        for (std::int32_t row = 0; row < image.rows(); ++row) {
            for (std::int32_t col = 0; col < image.cols(); ++col) {
                const float value = std::clamp(image(row, col), 0.0f, 255.0f);
                const std::uint8_t byte = static_cast<std::uint8_t>(std::lround(value));
                output.write(reinterpret_cast<const char*>(&byte), 1);
            }
        }
    } else {
        for (std::int32_t row = 0; row < image.rows(); ++row) {
            for (std::int32_t col = 0; col < image.cols(); ++col) {
                writeF32Be(output, image(row, col));
            }
        }
    }

    if (!output) {
        error = "Failed to write image data.";
        return false;
    }
    const auto imageChunkEnd = output.tellp();
    if (!finalizeChunkHeader(output, imageChunkStart, imageChunkEnd)) {
        error = "Failed to finalize image chunk header.";
        return false;
    }

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
    if (!output) {
        error = "Failed to write annotation chunk header.";
        return false;
    }

    std::size_t written = 0;
    writeZeros(output, RpfConstants::kAnnotationHeaderSize);
    written += RpfConstants::kAnnotationHeaderSize;

    writeI32Be(output, options.fileType);
    writeI32Be(output, options.radarMode);
    writeZeros(output, RpfConstants::kProcImgFileIdSize - 8);
    written += RpfConstants::kProcImgFileIdSize;

    writeZeros(output, RpfConstants::kImgDisplayParamSize);
    written += RpfConstants::kImgDisplayParamSize;
    writeZeros(output, RpfConstants::kDataAcqInfoSize);
    written += RpfConstants::kDataAcqInfoSize;
    writeZeros(output, RpfConstants::kSeaspotTargetSize);
    written += RpfConstants::kSeaspotTargetSize;
    writeZeros(output, RpfConstants::kLandspotTargetSize);
    written += RpfConstants::kLandspotTargetSize;
    writeZeros(output, RpfConstants::kStripmapTargetSize);
    written += RpfConstants::kStripmapTargetSize;
    writeZeros(output, RpfConstants::kProcInParamSize);
    written += RpfConstants::kProcInParamSize;

    writeZeros(output, RpfConstants::kDataProcOutputSize - RpfConstants::kDataProcOutputTailSize);
    for (int i = 0; i < 8; ++i) {
        writeU32Be(output, 0);
        writeU32Be(output, 0);
    }
    writeI32Be(output, options.geolocationGridNumLines);
    written += RpfConstants::kDataProcOutputSize - RpfConstants::kDataProcOutputTailSize;
    written += 8u * 8u;
    written += 4u;

    const std::uint16_t gridLines = options.geolocationGridNumLines;
    for (std::uint16_t i = 0; i < gridLines; ++i) {
        const std::int32_t lineNumber = 1 + static_cast<std::int32_t>(i) *
                                              std::max<std::int32_t>(1, height / gridLines);
        writeI32Be(output, lineNumber);
        writeF32Be(output, 1.0f);
        writeF32Be(output, 1.0f);
        writeF32Be(output, 1.0f);

        const double lat = static_cast<double>(i);
        const double lon = static_cast<double>(i);
        writeF64Be(output, lat);
        writeF64Be(output, lon);
        writeF64Be(output, lat + 0.5);
        writeF64Be(output, lon + 0.5);
        writeF64Be(output, lat + 1.0);
        writeF64Be(output, lon + 1.0);
        written += 64u;
    }

    if (written > fixedAnnotationBytes) {
        error = "RPF writer overflowed annotation block.";
        return false;
    }
    if (fixedAnnotationBytes > written) {
        writeZeros(output, fixedAnnotationBytes - written);
    }
    const auto annotationChunkEnd = output.tellp();
    if (!finalizeChunkHeader(output, annotationChunkStart, annotationChunkEnd)) {
        error = "Failed to finalize annotation chunk header.";
        return false;
    }

    if (!writeChunkHeader(output, RpfConstants::kEndOfFileChunkTag, 0)) {
        error = "Failed to write end-of-file chunk.";
        return false;
    }

    if (!output) {
        error = "RPF writer failed while writing data.";
        return false;
    }

    return true;
}

}  // namespace rpf
