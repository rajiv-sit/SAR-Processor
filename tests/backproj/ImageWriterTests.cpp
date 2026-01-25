#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "backproj/ImageWriter.hpp"

namespace {

std::filesystem::path makeTempPath(const std::string& stem) {
    const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() /
           (stem + "_" + std::to_string(now) + ".tif");
}

std::uint16_t readU16Le(std::ifstream& input) {
    std::uint8_t lo = 0;
    std::uint8_t hi = 0;
    input.read(reinterpret_cast<char*>(&lo), 1);
    input.read(reinterpret_cast<char*>(&hi), 1);
    return static_cast<std::uint16_t>(lo | (hi << 8));
}

std::uint32_t readU32Le(std::ifstream& input) {
    std::uint8_t b0 = 0;
    std::uint8_t b1 = 0;
    std::uint8_t b2 = 0;
    std::uint8_t b3 = 0;
    input.read(reinterpret_cast<char*>(&b0), 1);
    input.read(reinterpret_cast<char*>(&b1), 1);
    input.read(reinterpret_cast<char*>(&b2), 1);
    input.read(reinterpret_cast<char*>(&b3), 1);
    return static_cast<std::uint32_t>(b0 |
                                      (b1 << 8) |
                                      (b2 << 16) |
                                      (b3 << 24));
}

struct TiffInfo {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t stripOffset = 0;
    std::uint32_t stripByteCount = 0;
};

bool readTiffInfo(const std::filesystem::path& path,
                  TiffInfo& info,
                  std::vector<std::uint16_t>& pixels) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return false;
    }

    char endian[2] = {};
    input.read(endian, 2);
    if (endian[0] != 'I' || endian[1] != 'I') {
        return false;
    }
    const std::uint16_t magic = readU16Le(input);
    if (magic != 42) {
        return false;
    }
    const std::uint32_t ifdOffset = readU32Le(input);
    input.seekg(static_cast<std::streamoff>(ifdOffset), std::ios::beg);
    if (!input) {
        return false;
    }

    const std::uint16_t entryCount = readU16Le(input);
    for (std::uint16_t i = 0; i < entryCount; ++i) {
        const std::uint16_t tag = readU16Le(input);
        const std::uint16_t type = readU16Le(input);
        const std::uint32_t count = readU32Le(input);
        const std::uint32_t value = readU32Le(input);
        (void)type;
        (void)count;
        switch (tag) {
            case 256:
                info.width = value;
                break;
            case 257:
                info.height = value;
                break;
            case 273:
                info.stripOffset = value;
                break;
            case 279:
                info.stripByteCount = value;
                break;
            default:
                break;
        }
    }

    if (info.width == 0 || info.height == 0 || info.stripOffset == 0) {
        return false;
    }

    input.seekg(static_cast<std::streamoff>(info.stripOffset), std::ios::beg);
    if (!input) {
        return false;
    }

    const std::size_t count = static_cast<std::size_t>(info.width) *
                              static_cast<std::size_t>(info.height);
    pixels.assign(count, 0);
    for (std::size_t idx = 0; idx < count; ++idx) {
        pixels[idx] = readU16Le(input);
    }
    return static_cast<bool>(input);
}

std::uint16_t scaleToU16(float value, float minValue, float maxValue) {
    const float range = (maxValue > minValue) ? (maxValue - minValue) : 1.0f;
    const float normalized = (value - minValue) / range;
    const float clamped = std::clamp(normalized, 0.0f, 1.0f);
    return static_cast<std::uint16_t>(clamped * 65535.0f + 0.5f);
}

}  // namespace

TEST(ImageWriterTests, WritesTiffFile) {
    Eigen::MatrixXf image(2, 2);
    image << 1.0f, 2.0f,
             3.0f, 4.0f;

    const auto path = makeTempPath("backproj_stub");
    ASSERT_TRUE(backproj::writeTiff(path.string(), image));
    EXPECT_TRUE(std::filesystem::exists(path));
}

TEST(ImageWriterTests, RejectsEmptyImage) {
    Eigen::MatrixXf image;
    const auto path = std::filesystem::temp_directory_path() / "empty.tif";
    EXPECT_FALSE(backproj::writeTiff(path.string(), image));
}

TEST(ImageWriterTests, FailsForInvalidPath) {
    Eigen::MatrixXf image(1, 1);
    image(0, 0) = 1.0f;
    const auto path = std::filesystem::temp_directory_path() / "no_dir" / "bad.tif";
    EXPECT_FALSE(backproj::writeTiff(path.string(), image));
}

TEST(ImageWriterTests, WritesTiffDimensionsAndScaling) {
    Eigen::MatrixXf image(2, 2);
    image << 1.0f, 2.0f,
             3.0f, 4.0f;

    const auto path = makeTempPath("backproj_dims");
    ASSERT_TRUE(backproj::writeTiff(path.string(), image));

    TiffInfo info{};
    std::vector<std::uint16_t> pixels;
    ASSERT_TRUE(readTiffInfo(path, info, pixels));
    EXPECT_EQ(info.width, 2u);
    EXPECT_EQ(info.height, 2u);
    ASSERT_EQ(pixels.size(), 4u);

    const float minValue = image.minCoeff();
    const float maxValue = image.maxCoeff();
    EXPECT_EQ(pixels[0], scaleToU16(1.0f, minValue, maxValue));
    EXPECT_EQ(pixels[1], scaleToU16(2.0f, minValue, maxValue));
    EXPECT_EQ(pixels[2], scaleToU16(3.0f, minValue, maxValue));
    EXPECT_EQ(pixels[3], scaleToU16(4.0f, minValue, maxValue));
}
