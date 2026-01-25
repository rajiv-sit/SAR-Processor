#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "rpf/RpfImageDataParser.hpp"

namespace {

std::filesystem::path makeTempPath(const std::string& stem) {
    const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() /
           (stem + "_" + std::to_string(now) + ".bin");
}

void writeU16Be(std::ofstream& output, std::uint16_t value) {
    const char bytes[2] = {
        static_cast<char>((value >> 8) & 0xFF),
        static_cast<char>(value & 0xFF)
    };
    output.write(bytes, 2);
}

void writeU32Be(std::ofstream& output, std::uint32_t value) {
    const char bytes[4] = {
        static_cast<char>((value >> 24) & 0xFF),
        static_cast<char>((value >> 16) & 0xFF),
        static_cast<char>((value >> 8) & 0xFF),
        static_cast<char>(value & 0xFF)
    };
    output.write(bytes, 4);
}

void writeFloatBe(std::ofstream& output, float value) {
    static_assert(sizeof(float) == sizeof(std::uint32_t));
    std::uint32_t raw = 0;
    std::memcpy(&raw, &value, sizeof(raw));
    writeU32Be(output, raw);
}

}  // namespace

TEST(RpfImageDataParserTests, ParsesUint8Pixels) {
    const auto path = makeTempPath("rpf_u8");
    std::ofstream output(path, std::ios::binary);
    ASSERT_TRUE(output);
    output.put(static_cast<char>(3));
    output.put(static_cast<char>(4));
    output.close();

    std::ifstream input(path, std::ios::binary);
    rpf::ImageDataChunkHeader header{};
    header.dataWidth = 2;
    header.dataHeight = 1;
    header.pixelType = 0;

    Eigen::MatrixXf image;
    rpf::RpfImageDataParser parser;
    ASSERT_TRUE(parser.parseImageData(input, header, false, image));
    EXPECT_EQ(image.rows(), 1);
    EXPECT_EQ(image.cols(), 2);
    EXPECT_FLOAT_EQ(image(0, 0), 3.0f);
    EXPECT_FLOAT_EQ(image(0, 1), 4.0f);
}

TEST(RpfImageDataParserTests, ParsesHalfFloatPixels) {
    const auto path = makeTempPath("rpf_half");
    std::ofstream output(path, std::ios::binary);
    ASSERT_TRUE(output);
    writeU16Be(output, 0x3C00);  // 1.0
    writeU16Be(output, 0xC000);  // -2.0
    output.close();

    std::ifstream input(path, std::ios::binary);
    rpf::ImageDataChunkHeader header{};
    header.dataWidth = 2;
    header.dataHeight = 1;
    header.pixelType = 4;

    Eigen::MatrixXf image;
    rpf::RpfImageDataParser parser;
    ASSERT_TRUE(parser.parseImageData(input, header, false, image));
    EXPECT_FLOAT_EQ(image(0, 0), 1.0f);
    EXPECT_FLOAT_EQ(image(0, 1), -2.0f);
}

TEST(RpfImageDataParserTests, ParsesComplexFloatMagnitude) {
    const auto path = makeTempPath("rpf_cfloat");
    std::ofstream output(path, std::ios::binary);
    ASSERT_TRUE(output);
    writeFloatBe(output, 3.0f);
    writeFloatBe(output, 4.0f);
    output.close();

    std::ifstream input(path, std::ios::binary);
    rpf::ImageDataChunkHeader header{};
    header.dataWidth = 1;
    header.dataHeight = 1;
    header.pixelType = 5;

    Eigen::MatrixXf image;
    rpf::RpfImageDataParser parser;
    ASSERT_TRUE(parser.parseImageData(input, header, false, image));
    EXPECT_FLOAT_EQ(image(0, 0), 5.0f);
}

TEST(RpfImageDataParserTests, ParsesComplexHalfMagnitude) {
    const auto path = makeTempPath("rpf_chalf");
    std::ofstream output(path, std::ios::binary);
    ASSERT_TRUE(output);
    writeU16Be(output, 0x3C00);  // 1.0
    writeU16Be(output, 0x0000);  // 0.0
    output.close();

    std::ifstream input(path, std::ios::binary);
    rpf::ImageDataChunkHeader header{};
    header.dataWidth = 1;
    header.dataHeight = 1;
    header.pixelType = 6;

    Eigen::MatrixXf image;
    rpf::RpfImageDataParser parser;
    ASSERT_TRUE(parser.parseImageData(input, header, false, image));
    EXPECT_FLOAT_EQ(image(0, 0), 1.0f);
}

TEST(RpfImageDataParserTests, RejectsZeroDimensionsWhenNotSkipping) {
    const auto path = makeTempPath("rpf_zero");
    std::ofstream output(path, std::ios::binary);
    output.close();

    std::ifstream input(path, std::ios::binary);
    rpf::ImageDataChunkHeader header{};
    header.dataWidth = 0;
    header.dataHeight = 0;

    Eigen::MatrixXf image;
    rpf::RpfImageDataParser parser;
    EXPECT_FALSE(parser.parseImageData(input, header, false, image));
}

TEST(RpfImageDataParserTests, ParsesFloatPixelsForDefaultType) {
    const auto path = makeTempPath("rpf_f32");
    std::ofstream output(path, std::ios::binary);
    ASSERT_TRUE(output);
    writeFloatBe(output, 1.5f);
    writeFloatBe(output, -2.5f);
    output.close();

    std::ifstream input(path, std::ios::binary);
    rpf::ImageDataChunkHeader header{};
    header.dataWidth = 2;
    header.dataHeight = 1;
    header.pixelType = 3;

    Eigen::MatrixXf image;
    rpf::RpfImageDataParser parser;
    ASSERT_TRUE(parser.parseImageData(input, header, false, image));
    EXPECT_NEAR(image(0, 0), 1.5f, 1e-6f);
    EXPECT_NEAR(image(0, 1), -2.5f, 1e-6f);
}

TEST(RpfImageDataParserTests, SkipsImageDataWhenConfigured) {
    const auto path = makeTempPath("rpf_skip");
    std::ofstream output(path, std::ios::binary);
    ASSERT_TRUE(output);
    output.put(static_cast<char>(0xAA));
    output.put(static_cast<char>(0xBB));
    output.close();

    std::ifstream input(path, std::ios::binary);
    rpf::ImageDataChunkHeader header{};
    header.dataWidth = 2;
    header.dataHeight = 1;
    header.pixelType = 0;

    Eigen::MatrixXf image;
    rpf::RpfImageDataParser parser;
    ASSERT_TRUE(parser.parseImageData(input, header, true, image));
    EXPECT_EQ(image.size(), 0);
}

TEST(RpfImageDataParserTests, RejectsZeroDimensionsWhenSkipping) {
    const auto path = makeTempPath("rpf_skip_zero");
    std::ofstream output(path, std::ios::binary);
    output.close();

    std::ifstream input(path, std::ios::binary);
    rpf::ImageDataChunkHeader header{};
    header.dataWidth = 0;
    header.dataHeight = 0;

    Eigen::MatrixXf image;
    rpf::RpfImageDataParser parser;
    EXPECT_FALSE(parser.parseImageData(input, header, true, image));
}
