#include "rpf/RpfImageDataParser.hpp"

#include <array>
#include <cmath>
#include <cstring>
#include <fstream>

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

float readFloatBe(std::ifstream& input) {
    std::uint32_t raw = 0;
    if (!readU32Be(input, raw)) {
        return 0.0f;
    }
    float value = 0.0f;
    std::memcpy(&value, &raw, sizeof(value));
    return value;
}

float readHalfBe(std::ifstream& input) {
    std::uint16_t raw = 0;
    if (!readU16Be(input, raw)) {
        return 0.0f;
    }
    return halfToFloat(raw);
}

float readU8(std::ifstream& input) {
    std::uint8_t value = 0;
    input.read(reinterpret_cast<char*>(&value), 1);
    return static_cast<float>(value);
}

}  // namespace

bool RpfImageDataParser::parseImageData(std::ifstream& input,
                                        const ImageDataChunkHeader& header,
                                        bool skipImageData,
                                        Eigen::MatrixXf& outImage) {
    if (skipImageData) {
        const std::uint32_t width = header.dataWidth;
        const std::uint32_t height = header.dataHeight;
        if (width == 0 || height == 0) {
            return false;
        }
        std::size_t bytesPerPixel = 4;
        switch (header.pixelType) {
            case 0:
            case 1:
                bytesPerPixel = 1;
                break;
            case 4:
                bytesPerPixel = 2;
                break;
            case 5:
                bytesPerPixel = 8;
                break;
            case 6:
                bytesPerPixel = 4;
                break;
            default:
                bytesPerPixel = 4;
                break;
        }
        const std::size_t bytesToSkip =
            static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * bytesPerPixel;
        input.seekg(static_cast<std::streamoff>(bytesToSkip), std::ios::cur);
        return static_cast<bool>(input);
    }

    const std::uint32_t width = header.dataWidth;
    const std::uint32_t height = header.dataHeight;
    if (width == 0 || height == 0) {
        return false;
    }

    outImage.resize(static_cast<int>(height), static_cast<int>(width));

    for (std::uint32_t row = 0; row < height; ++row) {
        for (std::uint32_t col = 0; col < width; ++col) {
            float value = 0.0f;
            switch (header.pixelType) {
                case 0:
                case 1: {
                    value = readU8(input);
                    break;
                }
                case 4: {  // half float
                    value = readHalfBe(input);
                    break;
                }
                case 5: {  // complex float
                    const float real = readFloatBe(input);
                    const float imag = readFloatBe(input);
                    value = std::sqrt(real * real + imag * imag);
                    break;
                }
                case 6: {  // complex half
                    const float real = readHalfBe(input);
                    const float imag = readHalfBe(input);
                    value = std::sqrt(real * real + imag * imag);
                    break;
                }
                default: {  // float32
                    value = readFloatBe(input);
                    break;
                }
            }
            outImage(static_cast<int>(row), static_cast<int>(col)) = value;
        }
    }

    return static_cast<bool>(input);
}

}  // namespace rpf
