#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
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

TEST(RpfImageDataParserTests, HandlesHalfSubnormalAndInf) {
    const auto path = makeTempPath("rpf_half_special");
    std::ofstream output(path, std::ios::binary);
    ASSERT_TRUE(output);
    writeU16Be(output, 0x0001);
    writeU16Be(output, 0x7C00);
    output.close();

    std::ifstream input(path, std::ios::binary);
    rpf::ImageDataChunkHeader header{};
    header.dataWidth = 2;
    header.dataHeight = 1;
    header.pixelType = 4;

    Eigen::MatrixXf image;
    rpf::RpfImageDataParser parser;
    ASSERT_TRUE(parser.parseImageData(input, header, false, image));
    EXPECT_GT(image(0, 0), 0.0f);
    EXPECT_TRUE(std::isinf(image(0, 1)));
}

TEST(RpfImageDataParserTests, ParsesUnsignedIntegerTypesAndSkipsExactBytes) {
    for (const int type : {1, 2}) {
        const auto path = makeTempPath("rpf_unsigned");
        std::ofstream output(path, std::ios::binary);
        if (type == 1) {
            writeU16Be(output, 513);
            writeU16Be(output, 65535);
        } else {
            writeU32Be(output, 513);
            writeU32Be(output, 65535);
        }
        output.put('X');
        output.close();
        rpf::ImageDataChunkHeader header{};
        header.dataWidth = 2;
        header.dataHeight = 1;
        header.pixelType = type;
        rpf::RpfImageDataParser parser;
        for (const bool skip : {false, true}) {
            std::ifstream input(path, std::ios::binary);
            Eigen::MatrixXf image;
            ASSERT_TRUE(parser.parseImageData(input, header, skip, image));
            if (!skip) {
                EXPECT_FLOAT_EQ(image(0, 0), 513.0f);
                EXPECT_FLOAT_EQ(image(0, 1), 65535.0f);
            }
            EXPECT_EQ(input.get(), 'X');
        }
    }
}

TEST(RpfImageDataParserTests, RejectsTruncatedPayloadForEveryTypeIncludingSkip) {
    constexpr int sizes[] = {1, 2, 4, 4, 2, 8, 4};
    rpf::RpfImageDataParser parser;
    for (int type = 0; type <= 6; ++type) {
        for (const bool skip : {false, true}) {
            const auto path = makeTempPath("rpf_truncated");
            std::ofstream output(path, std::ios::binary);
            for (int byte = 0; byte < 2 * sizes[type] - 1; ++byte) output.put(0);
            output.close();
            std::ifstream input(path, std::ios::binary);
            rpf::ImageDataChunkHeader header{};
            header.dataWidth = 2;
            header.dataHeight = 1;
            header.pixelType = type;
            Eigen::MatrixXf image = Eigen::MatrixXf::Constant(1, 1, 123.0f);
            EXPECT_FALSE(parser.parseImageData(input, header, skip, image));
            EXPECT_FLOAT_EQ(image(0, 0), 123.0f);
        }
    }
}

TEST(RpfImageDataParserTests, RejectsInvalidTypesAndOversizedDimensions) {
    const auto path = makeTempPath("rpf_invalid_header");
    std::ofstream(path, std::ios::binary).put(0);
    rpf::RpfImageDataParser parser;
    for (const int type : {-1, 7}) {
        std::ifstream input(path, std::ios::binary);
        rpf::ImageDataChunkHeader header{};
        header.dataWidth = header.dataHeight = 1;
        header.pixelType = type;
        Eigen::MatrixXf image;
        EXPECT_FALSE(parser.parseImageData(input, header, false, image));
    }
    for (const bool width : {false, true}) {
        std::ifstream input(path, std::ios::binary);
        rpf::ImageDataChunkHeader header{};
        header.dataWidth = width ? std::numeric_limits<std::uint32_t>::max() : 1;
        header.dataHeight = width ? 1 : std::numeric_limits<std::uint32_t>::max();
        Eigen::MatrixXf image;
        EXPECT_FALSE(parser.parseImageData(input, header, false, image));
    }
}

TEST(RpfImageDataParserTests, PreservesHalfNanSignedZeroAndInfinity) {
    const auto path = makeTempPath("rpf_half_ieee");
    std::ofstream output(path, std::ios::binary);
    for (const std::uint16_t bits : {0x7E00, 0xFE00, 0x0000, 0x8000, 0xFC00, 0x8001}) {
        writeU16Be(output, bits);
    }
    output.close();
    std::ifstream input(path, std::ios::binary);
    rpf::ImageDataChunkHeader header{};
    header.dataWidth = 6;
    header.dataHeight = 1;
    header.pixelType = 4;
    Eigen::MatrixXf image;
    rpf::RpfImageDataParser parser;
    ASSERT_TRUE(parser.parseImageData(input, header, false, image));
    EXPECT_TRUE(std::isnan(image(0, 0)));
    EXPECT_TRUE(std::isnan(image(0, 1)));
    EXPECT_FALSE(std::signbit(image(0, 2)));
    EXPECT_TRUE(std::signbit(image(0, 3)));
    EXPECT_EQ(image(0, 4), -std::numeric_limits<float>::infinity());
    EXPECT_LT(image(0, 5), 0.0f);
}
