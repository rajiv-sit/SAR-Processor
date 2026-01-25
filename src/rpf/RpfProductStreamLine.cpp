#include "rpf/RpfProductStreamLine.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>

#include "rpf/AnnotationStruct.hpp"
#include "rpf/RpfChunkReader.hpp"
#include "rpf/RpfConstants.hpp"
#include "rpf/RpfFileUtils.hpp"
#include "rpf/RpfQuery.hpp"

namespace rpf {

namespace {

int bytesPerPixel(int pixelType) {
    switch (pixelType) {
        case 0:
            return 1;
        case 1:
            return 2;
        case 2:
            return 4;
        case 3:
            return 4;
        case 4:
            return 2;
        case 5:
            return 8;
        case 6:
            return 4;
        default:
            return 0;
    }
}

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

float halfToFloat(std::uint16_t value) {
    const std::uint16_t sign = (value >> 15) & 0x1;
    const std::uint16_t exp = (value >> 10) & 0x1F;
    const std::uint16_t mantissa = value & 0x3FF;

    if (exp == 0) {
        if (mantissa == 0) {
            return sign ? -0.0f : 0.0f;
        }
        const float m = static_cast<float>(mantissa) / 1024.0f;
        const float val = std::ldexp(m, -14);
        return sign ? -val : val;
    }
    if (exp == 31) {
        return sign ? -INFINITY : INFINITY;
    }
    const float m = 1.0f + static_cast<float>(mantissa) / 1024.0f;
    const float val = std::ldexp(m, static_cast<int>(exp) - 15);
    return sign ? -val : val;
}

bool readFloatBe(std::ifstream& input, float& value) {
    std::uint32_t raw = 0;
    if (!readU32Be(input, raw)) {
        return false;
    }
    std::memcpy(&value, &raw, sizeof(value));
    return true;
}

bool readHalfBe(std::ifstream& input, float& value) {
    std::uint16_t raw = 0;
    if (!readU16Be(input, raw)) {
        return false;
    }
    value = halfToFloat(raw);
    return true;
}

bool readLineData(std::ifstream& input,
                  int pixelType,
                  int pixels,
                  std::vector<float>& lineData) {
    lineData.assign(static_cast<std::size_t>(pixels), 0.0f);
    for (int i = 0; i < pixels; ++i) {
        float value = 0.0f;
        switch (pixelType) {
            case 0: {
                std::uint8_t v = 0;
                if (!readBytes(input, &v, 1)) {
                    return false;
                }
                value = static_cast<float>(v);
                break;
            }
            case 1: {
                std::uint16_t v = 0;
                if (!readU16Be(input, v)) {
                    return false;
                }
                value = static_cast<float>(v);
                break;
            }
            case 2: {
                std::uint32_t v = 0;
                if (!readU32Be(input, v)) {
                    return false;
                }
                value = static_cast<float>(v);
                break;
            }
            case 4: {
                if (!readHalfBe(input, value)) {
                    return false;
                }
                break;
            }
            case 5: {
                float real = 0.0f;
                float imag = 0.0f;
                if (!readFloatBe(input, real) || !readFloatBe(input, imag)) {
                    return false;
                }
                value = std::sqrt(real * real + imag * imag);
                break;
            }
            case 6: {
                float real = 0.0f;
                float imag = 0.0f;
                if (!readHalfBe(input, real) || !readHalfBe(input, imag)) {
                    return false;
                }
                value = std::sqrt(real * real + imag * imag);
                break;
            }
            default: {
                if (!readFloatBe(input, value)) return false;
                break;
            }
        }
        lineData[static_cast<std::size_t>(i)] = value;
    }
    return true;
}

void appendLatLong(LatLongGrid& target, const LatLongGrid& source) {
    target.lineNumber.insert(target.lineNumber.end(), source.lineNumber.begin(), source.lineNumber.end());
    target.beginGrSrRatio.insert(target.beginGrSrRatio.end(), source.beginGrSrRatio.begin(), source.beginGrSrRatio.end());
    target.midGrSrRatio.insert(target.midGrSrRatio.end(), source.midGrSrRatio.begin(), source.midGrSrRatio.end());
    target.endGrSrRatio.insert(target.endGrSrRatio.end(), source.endGrSrRatio.begin(), source.endGrSrRatio.end());
    target.beginLatitude.insert(target.beginLatitude.end(), source.beginLatitude.begin(), source.beginLatitude.end());
    target.beginLongitude.insert(target.beginLongitude.end(), source.beginLongitude.begin(), source.beginLongitude.end());
    target.midLatitude.insert(target.midLatitude.end(), source.midLatitude.begin(), source.midLatitude.end());
    target.midLongitude.insert(target.midLongitude.end(), source.midLongitude.begin(), source.midLongitude.end());
    target.endLatitude.insert(target.endLatitude.end(), source.endLatitude.begin(), source.endLatitude.end());
    target.endLongitude.insert(target.endLongitude.end(), source.endLongitude.begin(), source.endLongitude.end());
}

}  // namespace

bool RpfProductStreamLine::init(const std::string& fileName,
                                int frameNum,
                                RpfProductStreamLine& stream,
                                std::string& error) {
    stream.blocks_.clear();
    stream.latLongGrid_ = {};

    RpfQueryResult query{};
    if (!queryRpfFile(fileName, query) || query.startNums.empty()) {
        error = "Failed to query file";
        return false;
    }

    const bool isStripmap = (query.mode == RpfConstants::kStripmapMode);
    const std::string baseName = getBaseFileName(fileName);
    std::vector<std::string> files;
    if (isStripmap) {
        files = findFiles(baseName);
        if (files.empty()) {
            error = "No files found for stripmap acquisition";
            return false;
        }
    }
    if (!isStripmap) {
        files = {fileName};
        if (frameNum <= 0) {
            frameNum = 1;
        }
    }

    bool foundFrame = isStripmap;
    for (const auto& path : files) {
        RpfQueryResult fileQuery{};
        if (!queryRpfFile(path, fileQuery)) continue;

        for (std::size_t idx = 0; idx < fileQuery.startNums.size(); ++idx) {
            if (!isStripmap && fileQuery.startNums[idx] != frameNum) {
                continue;
            }
            RpfStreamBlock block{};
            block.path = path;
            block.startLine = isStripmap ? fileQuery.startNums[idx] : 1;
            block.numLines = fileQuery.numLines[idx];
            block.numPixels = fileQuery.numPixels[idx];
            block.pixelType = fileQuery.pixelTypes[idx];
            block.bofImgOffset = fileQuery.bofImgOffsets[idx];
            stream.blocks_.push_back(block);

            if (!isStripmap) {
                foundFrame = true;
            }
        }

        if (!isStripmap && foundFrame) {
            break;
        }
    }

    if (!foundFrame || stream.blocks_.empty()) {
        error = "Frame not found";
        return false;
    }

    for (std::size_t i = 0; i < stream.blocks_.size(); ++i) {
        rpf::AnnotationStruct annotation{};
        rpf::LatLongGrid grid{};
        RpfChunkReader reader(stream.blocks_[i].path);
        if (!reader.isOpen()) continue;
        if (reader.readBlock(static_cast<std::uint32_t>(i + 1), annotation, grid, true)) {
            appendLatLong(stream.latLongGrid_, grid);
        }
    }

    return true;
}

RpfProductStreamLine RpfProductStreamLine::makeSynthetic(const std::vector<RpfStreamBlock>& blocks,
                                                         const LatLongGrid& grid) {
    RpfProductStreamLine stream;
    stream.blocks_ = blocks;
    stream.latLongGrid_ = grid;
    return stream;
}

bool RpfProductStreamLine::readLine(int lineNum, std::vector<float>& lineData) const {
    if (blocks_.empty()) return false;

    const RpfStreamBlock* target = nullptr;
    for (const auto& block : blocks_) {
        const int endLine = block.startLine + block.numLines - 1;
        if (lineNum >= block.startLine && lineNum <= endLine) {
            target = &block;
            break;
        }
    }
    if (!target) {
        return false;
    }

    const int pixelBytes = bytesPerPixel(target->pixelType);
    if (pixelBytes <= 0) {
        return false;
    }
    const std::int64_t lineOffset = static_cast<std::int64_t>(lineNum - target->startLine);
    const std::int64_t byteOffset =
        static_cast<std::int64_t>(target->bofImgOffset) +
        lineOffset * static_cast<std::int64_t>(pixelBytes) *
            static_cast<std::int64_t>(target->numPixels);

    std::ifstream input(target->path, std::ios::binary);
    if (!input) return false;
    input.seekg(byteOffset, std::ios::beg);
    if (!input) return false;

    return readLineData(input, target->pixelType, target->numPixels, lineData);
}

bool RpfProductStreamLine::getLatLong(int line, int pixel, double& lat, double& lon, double& grToSr) const {
    if (latLongGrid_.lineNumber.empty() || blocks_.empty()) {
        return false;
    }

    int numPixels = blocks_.front().numPixels;
    int numLines = 0;
    for (const auto& block : blocks_) {
        numLines += block.numLines;
    }

    int gridLineIdx = 0;
    bool interpRequired = true;
    for (std::size_t g = 0; g < latLongGrid_.lineNumber.size(); ++g) {
        if (line == latLongGrid_.lineNumber[g]) {
            gridLineIdx = static_cast<int>(g);
            interpRequired = false;
            break;
        }
        if (line < latLongGrid_.lineNumber[g]) {
            gridLineIdx = static_cast<int>(g) - 1;
            break;
        }
    }

    if (interpRequired) {
        if (line < latLongGrid_.lineNumber.front()) {
            gridLineIdx = 0;
        } else if (line > latLongGrid_.lineNumber.back()) {
            gridLineIdx = static_cast<int>(latLongGrid_.lineNumber.size()) - 2;
        }
    }
    if (gridLineIdx < 0 || gridLineIdx >= static_cast<int>(latLongGrid_.lineNumber.size())) {
        return false;
    }

    auto gridLine = std::array<std::array<double, 3>, 3>{};
    gridLine[0][0] = latLongGrid_.beginLatitude[gridLineIdx];
    gridLine[0][1] = latLongGrid_.beginLongitude[gridLineIdx];
    gridLine[0][2] = latLongGrid_.beginGrSrRatio[gridLineIdx];
    gridLine[1][0] = latLongGrid_.midLatitude[gridLineIdx];
    gridLine[1][1] = latLongGrid_.midLongitude[gridLineIdx];
    gridLine[1][2] = latLongGrid_.midGrSrRatio[gridLineIdx];
    gridLine[2][0] = latLongGrid_.endLatitude[gridLineIdx];
    gridLine[2][1] = latLongGrid_.endLongitude[gridLineIdx];
    gridLine[2][2] = latLongGrid_.endGrSrRatio[gridLineIdx];

    if (interpRequired && gridLineIdx + 1 < static_cast<int>(latLongGrid_.lineNumber.size())) {
        const int line1 = latLongGrid_.lineNumber[gridLineIdx];
        const int line2 = latLongGrid_.lineNumber[gridLineIdx + 1];
        if (line2 != line1) {
            const double perc = static_cast<double>(line - line1) / static_cast<double>(line2 - line1);
            auto lerp = [perc](double a, double b) { return a + perc * (b - a); };
            gridLine[0][0] = lerp(gridLine[0][0], latLongGrid_.beginLatitude[gridLineIdx + 1]);
            gridLine[0][1] = lerp(gridLine[0][1], latLongGrid_.beginLongitude[gridLineIdx + 1]);
            gridLine[0][2] = lerp(gridLine[0][2], latLongGrid_.beginGrSrRatio[gridLineIdx + 1]);
            gridLine[1][0] = lerp(gridLine[1][0], latLongGrid_.midLatitude[gridLineIdx + 1]);
            gridLine[1][1] = lerp(gridLine[1][1], latLongGrid_.midLongitude[gridLineIdx + 1]);
            gridLine[1][2] = lerp(gridLine[1][2], latLongGrid_.midGrSrRatio[gridLineIdx + 1]);
            gridLine[2][0] = lerp(gridLine[2][0], latLongGrid_.endLatitude[gridLineIdx + 1]);
            gridLine[2][1] = lerp(gridLine[2][1], latLongGrid_.endLongitude[gridLineIdx + 1]);
            gridLine[2][2] = lerp(gridLine[2][2], latLongGrid_.endGrSrRatio[gridLineIdx + 1]);
        }
    }

    const std::array<int, 3> gridPixelPoints{
        1,
        (numPixels + 1) / 2,
        numPixels
    };
    int gridPixelIdx = 0;
    bool pixelInterp = true;
    for (int i = 0; i < 3; ++i) {
        if (pixel == gridPixelPoints[i]) {
            gridPixelIdx = i;
            pixelInterp = false;
            break;
        }
        if (pixel < gridPixelPoints[i]) {
            gridPixelIdx = i - 1;
            break;
        }
    }
    if (pixelInterp) {
        if (pixel < gridPixelPoints[0]) {
            gridPixelIdx = 0;
        } else if (pixel > gridPixelPoints[2]) {
            gridPixelIdx = 1;
        }
    }
    if (gridPixelIdx < 0 || gridPixelIdx > 2) return false;

    lat = gridLine[gridPixelIdx][0];
    lon = gridLine[gridPixelIdx][1];
    grToSr = gridLine[gridPixelIdx][2];
    if (pixelInterp && gridPixelIdx + 1 < 3) {
        const double perc = static_cast<double>(pixel - gridPixelPoints[gridPixelIdx]) /
                            static_cast<double>(gridPixelPoints[gridPixelIdx + 1] - gridPixelPoints[gridPixelIdx]);
        auto lerp = [perc](double a, double b) { return a + perc * (b - a); };
        lat = lerp(lat, gridLine[gridPixelIdx + 1][0]);
        lon = lerp(lon, gridLine[gridPixelIdx + 1][1]);
        grToSr = lerp(grToSr, gridLine[gridPixelIdx + 1][2]);
    }

    (void)numLines;
    return true;
}

}  // namespace rpf
