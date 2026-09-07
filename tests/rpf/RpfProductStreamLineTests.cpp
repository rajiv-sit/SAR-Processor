#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <Eigen/Core>

#include "rpf/RpfProductStreamLine.hpp"
#include "rpf/RpfWriter.hpp"

namespace {

std::filesystem::path makeTempPath(const std::string& stem) {
    const auto name = stem + "_" + std::to_string(std::rand()) + ".rpf";
    return std::filesystem::temp_directory_path() / name;
}

void writeBytes(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes) {
    std::ofstream output(path, std::ios::binary);
    output.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
}

std::vector<float> readLineFromRpf(const std::filesystem::path& path,
                                   int frameNum,
                                   int lineNum) {
    rpf::RpfProductStreamLine stream;
    std::string error;
    if (!rpf::RpfProductStreamLine::init(path.string(), frameNum, stream, error)) {
        return {};
    }
    std::vector<float> line;
    if (!stream.readLine(lineNum, line)) {
        return {};
    }
    return line;
}

}  // namespace

TEST(RpfProductStreamLineTests, InterpolatesLatLongForLineAndPixel) {
    rpf::LatLongGrid grid{};
    grid.lineNumber = {1, 3};
    grid.beginLatitude = {0.0, 10.0};
    grid.beginLongitude = {0.0, 10.0};
    grid.beginGrSrRatio = {1.0, 2.0};
    grid.midLatitude = {10.0, 20.0};
    grid.midLongitude = {10.0, 20.0};
    grid.midGrSrRatio = {2.0, 3.0};
    grid.endLatitude = {20.0, 30.0};
    grid.endLongitude = {20.0, 30.0};
    grid.endGrSrRatio = {3.0, 4.0};

    rpf::RpfStreamBlock block{};
    block.startLine = 1;
    block.numLines = 3;
    block.numPixels = 5;
    block.pixelType = 0;

    const auto stream = rpf::RpfProductStreamLine::makeSynthetic({block}, grid);
    double lat = 0.0;
    double lon = 0.0;
    double gr = 0.0;
    ASSERT_TRUE(stream.getLatLong(2, 3, lat, lon, gr));
    EXPECT_DOUBLE_EQ(lat, 15.0);
    EXPECT_DOUBLE_EQ(lon, 15.0);
    EXPECT_DOUBLE_EQ(gr, 2.5);
}

TEST(RpfProductStreamLineTests, FailsWhenGridMissing) {
    rpf::RpfStreamBlock block{};
    block.startLine = 1;
    block.numLines = 2;
    block.numPixels = 4;
    block.pixelType = 0;

    rpf::LatLongGrid emptyGrid{};
    const auto stream = rpf::RpfProductStreamLine::makeSynthetic({block}, emptyGrid);

    double lat = 0.0;
    double lon = 0.0;
    double gr = 0.0;
    EXPECT_FALSE(stream.getLatLong(1, 1, lat, lon, gr));
}

TEST(RpfProductStreamLineTests, FailsWhenSingleGridLineOutOfRange) {
    rpf::LatLongGrid grid{};
    grid.lineNumber = {10};
    grid.beginLatitude = {0.0};
    grid.beginLongitude = {0.0};
    grid.beginGrSrRatio = {1.0};
    grid.midLatitude = {0.0};
    grid.midLongitude = {0.0};
    grid.midGrSrRatio = {1.0};
    grid.endLatitude = {0.0};
    grid.endLongitude = {0.0};
    grid.endGrSrRatio = {1.0};

    rpf::RpfStreamBlock block{};
    block.startLine = 1;
    block.numLines = 2;
    block.numPixels = 4;
    block.pixelType = 0;

    const auto stream = rpf::RpfProductStreamLine::makeSynthetic({block}, grid);
    double lat = 0.0;
    double lon = 0.0;
    double gr = 0.0;
    EXPECT_FALSE(stream.getLatLong(20, 1, lat, lon, gr));
}

TEST(RpfProductStreamLineTests, ReadsUint8LineData) {
    Eigen::MatrixXf image(2, 3);
    image << 1.0f, 2.0f, 3.0f,
             4.0f, 5.0f, 6.0f;
    rpf::RpfWriteOptions options{};
    options.pixelType = 0;
    options.radarMode = 1;
    const auto path = makeTempPath("rpf_u8");
    std::string error;
    ASSERT_TRUE(rpf::writeRpfFile(path.string(), image, options, error)) << error;

    const auto line = readLineFromRpf(path, 1, 1);
    ASSERT_EQ(line.size(), 3u);
    EXPECT_FLOAT_EQ(line[0], 1.0f);
    EXPECT_FLOAT_EQ(line[1], 2.0f);
    EXPECT_FLOAT_EQ(line[2], 3.0f);
}

TEST(RpfProductStreamLineTests, ReadsUint16LineData) {
    Eigen::MatrixXf image(1, 3);
    image << 10.0f, 20.0f, 30.0f;
    rpf::RpfWriteOptions options{};
    options.pixelType = 1;
    options.radarMode = 1;
    const auto path = makeTempPath("rpf_u16");
    std::string error;
    ASSERT_TRUE(rpf::writeRpfFile(path.string(), image, options, error)) << error;

    const auto line = readLineFromRpf(path, 1, 1);
    ASSERT_EQ(line.size(), 3u);
    EXPECT_FLOAT_EQ(line[0], 10.0f);
    EXPECT_FLOAT_EQ(line[1], 20.0f);
    EXPECT_FLOAT_EQ(line[2], 30.0f);
}

TEST(RpfProductStreamLineTests, ReadsUint32LineData) {
    Eigen::MatrixXf image(1, 2);
    image << 1000.0f, 2000.0f;
    rpf::RpfWriteOptions options{};
    options.pixelType = 2;
    options.radarMode = 1;
    const auto path = makeTempPath("rpf_u32");
    std::string error;
    ASSERT_TRUE(rpf::writeRpfFile(path.string(), image, options, error)) << error;

    const auto line = readLineFromRpf(path, 1, 1);
    ASSERT_EQ(line.size(), 2u);
    EXPECT_FLOAT_EQ(line[0], 1000.0f);
    EXPECT_FLOAT_EQ(line[1], 2000.0f);
}

TEST(RpfProductStreamLineTests, ReadsFloatLineDataForDefaultType) {
    Eigen::MatrixXf image(1, 2);
    image << 1.25f, 2.5f;
    rpf::RpfWriteOptions options{};
    options.pixelType = 3;
    options.radarMode = 1;
    const auto path = makeTempPath("rpf_f32");
    std::string error;
    ASSERT_TRUE(rpf::writeRpfFile(path.string(), image, options, error)) << error;

    const auto line = readLineFromRpf(path, 1, 1);
    ASSERT_EQ(line.size(), 2u);
    EXPECT_NEAR(line[0], 1.25f, 1e-5f);
    EXPECT_NEAR(line[1], 2.5f, 1e-5f);
}

TEST(RpfProductStreamLineTests, ReadsHalfFloatLineData) {
    Eigen::MatrixXf image(1, 2);
    image << 0.0f, 1.0f;
    rpf::RpfWriteOptions options{};
    options.pixelType = 4;
    options.radarMode = 1;
    const auto path = makeTempPath("rpf_half");
    std::string error;
    ASSERT_TRUE(rpf::writeRpfFile(path.string(), image, options, error)) << error;

    const auto line = readLineFromRpf(path, 1, 1);
    ASSERT_EQ(line.size(), 2u);
    EXPECT_NEAR(line[0], 0.0f, 1e-5f);
    EXPECT_NEAR(line[1], 1.0f, 1e-3f);
}

TEST(RpfProductStreamLineTests, ReadsHalfFloatSubnormalAndInf) {
    Eigen::MatrixXf image(1, 2);
    image << 1e-8f, std::numeric_limits<float>::infinity();
    rpf::RpfWriteOptions options{};
    options.pixelType = 4;
    options.radarMode = 1;
    const auto path = makeTempPath("rpf_half_special");
    std::string error;
    ASSERT_TRUE(rpf::writeRpfFile(path.string(), image, options, error)) << error;

    const auto line = readLineFromRpf(path, 1, 1);
    ASSERT_EQ(line.size(), 2u);
    EXPECT_GE(line[0], 0.0f);
    EXPECT_TRUE(std::isinf(line[1]));
}

TEST(RpfProductStreamLineTests, ReadsComplexFloatMagnitude) {
    Eigen::MatrixXf image(1, 2);
    image << 3.0f, 4.0f;
    rpf::RpfWriteOptions options{};
    options.pixelType = 5;
    options.radarMode = 1;
    const auto path = makeTempPath("rpf_cfloat");
    std::string error;
    ASSERT_TRUE(rpf::writeRpfFile(path.string(), image, options, error)) << error;

    const auto line = readLineFromRpf(path, 1, 1);
    ASSERT_EQ(line.size(), 2u);
    EXPECT_NEAR(line[0], 3.0f, 1e-5f);
    EXPECT_NEAR(line[1], 4.0f, 1e-5f);
}

TEST(RpfProductStreamLineTests, ReadsComplexHalfMagnitude) {
    Eigen::MatrixXf image(1, 2);
    image << 2.0f, 5.0f;
    rpf::RpfWriteOptions options{};
    options.pixelType = 6;
    options.radarMode = 1;
    const auto path = makeTempPath("rpf_chalf");
    std::string error;
    ASSERT_TRUE(rpf::writeRpfFile(path.string(), image, options, error)) << error;

    const auto line = readLineFromRpf(path, 1, 1);
    ASSERT_EQ(line.size(), 2u);
    EXPECT_NEAR(line[0], 2.0f, 1e-3f);
    EXPECT_NEAR(line[1], 5.0f, 1e-3f);
}

TEST(RpfProductStreamLineTests, ReturnsFalseForUnknownPixelType) {
    rpf::RpfStreamBlock block{};
    block.startLine = 1;
    block.numLines = 1;
    block.numPixels = 4;
    block.pixelType = 99;
    block.bofImgOffset = 0;
    const auto stream = rpf::RpfProductStreamLine::makeSynthetic({block}, {});

    std::vector<float> line;
    EXPECT_FALSE(stream.readLine(1, line));
}

TEST(RpfProductStreamLineTests, InitFailsForMissingFile) {
    rpf::RpfProductStreamLine stream;
    std::string error;
    EXPECT_FALSE(rpf::RpfProductStreamLine::init("missing_file.rpf", 1, stream, error));
    EXPECT_FALSE(error.empty());
}

TEST(RpfProductStreamLineTests, InitFailsForStripmapWithoutSequence) {
    Eigen::MatrixXf image(1, 1);
    image << 1.0f;
    rpf::RpfWriteOptions options{};
    options.pixelType = 0;
    options.radarMode = 2;
    const auto stem = "stripmapnocounter" + std::to_string(std::rand());
    const auto path = std::filesystem::temp_directory_path() / (stem + ".rpf");
    std::string error;
    ASSERT_TRUE(rpf::writeRpfFile(path.string(), image, options, error)) << error;

    rpf::RpfProductStreamLine stream;
    EXPECT_FALSE(rpf::RpfProductStreamLine::init(path.string(), 1, stream, error));
    EXPECT_FALSE(error.empty());
}

TEST(RpfProductStreamLineTests, InitDefaultsFrameNumberWhenZero) {
    Eigen::MatrixXf image(1, 1);
    image << 1.0f;
    rpf::RpfWriteOptions options{};
    options.pixelType = 0;
    options.radarMode = 1;
    const auto path = makeTempPath("frame_default");
    std::string error;
    ASSERT_TRUE(rpf::writeRpfFile(path.string(), image, options, error)) << error;

    rpf::RpfProductStreamLine stream;
    EXPECT_TRUE(rpf::RpfProductStreamLine::init(path.string(), 0, stream, error)) << error;
}

TEST(RpfProductStreamLineTests, InitFailsWhenFrameNotFound) {
    Eigen::MatrixXf image(1, 1);
    image << 1.0f;
    rpf::RpfWriteOptions options{};
    options.pixelType = 0;
    options.radarMode = 1;
    options.frameSeqNum = 2;
    const auto path = makeTempPath("frame_miss");
    std::string error;
    ASSERT_TRUE(rpf::writeRpfFile(path.string(), image, options, error)) << error;

    rpf::RpfProductStreamLine stream;
    EXPECT_FALSE(rpf::RpfProductStreamLine::init(path.string(), 1, stream, error));
    EXPECT_FALSE(error.empty());
}

TEST(RpfProductStreamLineTests, ReadLineReturnsFalseWhenOutOfRange) {
    Eigen::MatrixXf image(1, 2);
    image << 1.0f, 2.0f;
    rpf::RpfWriteOptions options{};
    options.pixelType = 0;
    options.radarMode = 1;
    const auto path = makeTempPath("line_oob");
    std::string error;
    ASSERT_TRUE(rpf::writeRpfFile(path.string(), image, options, error)) << error;

    rpf::RpfProductStreamLine stream;
    ASSERT_TRUE(rpf::RpfProductStreamLine::init(path.string(), 1, stream, error)) << error;
    std::vector<float> line;
    EXPECT_FALSE(stream.readLine(10, line));
}

TEST(RpfProductStreamLineTests, ReadLineFailsOnTruncatedUint8) {
    const auto path = makeTempPath("trunc_u8");
    writeBytes(path, {0x01});
    rpf::RpfStreamBlock block{};
    block.path = path.string();
    block.startLine = 1;
    block.numLines = 1;
    block.numPixels = 2;
    block.pixelType = 0;
    block.bofImgOffset = 0;
    const auto stream = rpf::RpfProductStreamLine::makeSynthetic({block}, {});
    std::vector<float> line;
    EXPECT_FALSE(stream.readLine(1, line));
}

TEST(RpfProductStreamLineTests, ReadLineFailsOnTruncatedUint16) {
    const auto path = makeTempPath("trunc_u16");
    writeBytes(path, {0x00});
    rpf::RpfStreamBlock block{};
    block.path = path.string();
    block.startLine = 1;
    block.numLines = 1;
    block.numPixels = 1;
    block.pixelType = 1;
    block.bofImgOffset = 0;
    const auto stream = rpf::RpfProductStreamLine::makeSynthetic({block}, {});
    std::vector<float> line;
    EXPECT_FALSE(stream.readLine(1, line));
}

TEST(RpfProductStreamLineTests, ReadLineFailsOnTruncatedUint32) {
    const auto path = makeTempPath("trunc_u32");
    writeBytes(path, {0x00, 0x00});
    rpf::RpfStreamBlock block{};
    block.path = path.string();
    block.startLine = 1;
    block.numLines = 1;
    block.numPixels = 1;
    block.pixelType = 2;
    block.bofImgOffset = 0;
    const auto stream = rpf::RpfProductStreamLine::makeSynthetic({block}, {});
    std::vector<float> line;
    EXPECT_FALSE(stream.readLine(1, line));
}

TEST(RpfProductStreamLineTests, ReadLineFailsOnTruncatedHalf) {
    const auto path = makeTempPath("trunc_half");
    writeBytes(path, {0x00});
    rpf::RpfStreamBlock block{};
    block.path = path.string();
    block.startLine = 1;
    block.numLines = 1;
    block.numPixels = 1;
    block.pixelType = 4;
    block.bofImgOffset = 0;
    const auto stream = rpf::RpfProductStreamLine::makeSynthetic({block}, {});
    std::vector<float> line;
    EXPECT_FALSE(stream.readLine(1, line));
}

TEST(RpfProductStreamLineTests, ReadLineFailsOnTruncatedComplexFloat) {
    const auto path = makeTempPath("trunc_cfloat");
    writeBytes(path, {0x00, 0x00, 0x00, 0x00});
    rpf::RpfStreamBlock block{};
    block.path = path.string();
    block.startLine = 1;
    block.numLines = 1;
    block.numPixels = 1;
    block.pixelType = 5;
    block.bofImgOffset = 0;
    const auto stream = rpf::RpfProductStreamLine::makeSynthetic({block}, {});
    std::vector<float> line;
    EXPECT_FALSE(stream.readLine(1, line));
}

TEST(RpfProductStreamLineTests, ReadLineFailsOnTruncatedComplexHalf) {
    const auto path = makeTempPath("trunc_chalf");
    writeBytes(path, {0x00});
    rpf::RpfStreamBlock block{};
    block.path = path.string();
    block.startLine = 1;
    block.numLines = 1;
    block.numPixels = 1;
    block.pixelType = 6;
    block.bofImgOffset = 0;
    const auto stream = rpf::RpfProductStreamLine::makeSynthetic({block}, {});
    std::vector<float> line;
    EXPECT_FALSE(stream.readLine(1, line));
}

TEST(RpfProductStreamLineTests, ReadsHalfSubnormalValue) {
    const auto path = makeTempPath("half_subnormal");
    writeBytes(path, {0x00, 0x01});
    rpf::RpfStreamBlock block{};
    block.path = path.string();
    block.startLine = 1;
    block.numLines = 1;
    block.numPixels = 1;
    block.pixelType = 4;
    block.bofImgOffset = 0;
    const auto stream = rpf::RpfProductStreamLine::makeSynthetic({block}, {});
    std::vector<float> line;
    ASSERT_TRUE(stream.readLine(1, line));
    ASSERT_EQ(line.size(), 1u);
    EXPECT_GT(line[0], 0.0f);
}

TEST(RpfProductStreamLineTests, PreservesHalfNan) {
    const auto path = makeTempPath("half_nan");
    writeBytes(path, {0x7E, 0x00});
    rpf::RpfStreamBlock block{};
    block.path = path.string();
    block.startLine = 1;
    block.numLines = 1;
    block.numPixels = 1;
    block.pixelType = 4;
    const auto stream = rpf::RpfProductStreamLine::makeSynthetic({block}, {});
    std::vector<float> line;
    ASSERT_TRUE(stream.readLine(1, line));
    ASSERT_EQ(line.size(), 1u);
    EXPECT_TRUE(std::isnan(line[0]));
}

TEST(RpfProductStreamLineTests, InterpolatesPixelWhenOffGrid) {
    rpf::LatLongGrid grid{};
    grid.lineNumber = {1, 3};
    grid.beginLatitude = {0.0, 10.0};
    grid.beginLongitude = {0.0, 10.0};
    grid.beginGrSrRatio = {1.0, 2.0};
    grid.midLatitude = {10.0, 20.0};
    grid.midLongitude = {10.0, 20.0};
    grid.midGrSrRatio = {2.0, 3.0};
    grid.endLatitude = {20.0, 30.0};
    grid.endLongitude = {20.0, 30.0};
    grid.endGrSrRatio = {3.0, 4.0};

    rpf::RpfStreamBlock block{};
    block.startLine = 1;
    block.numLines = 3;
    block.numPixels = 6;
    block.pixelType = 0;

    const auto stream = rpf::RpfProductStreamLine::makeSynthetic({block}, grid);
    double lat = 0.0;
    double lon = 0.0;
    double gr = 0.0;
    ASSERT_TRUE(stream.getLatLong(2, 2, lat, lon, gr));
    EXPECT_GT(lat, 0.0);
}

TEST(RpfProductStreamLineTests, HandlesLineBeforeFirstGrid) {
    rpf::LatLongGrid grid{};
    grid.lineNumber = {5, 10};
    grid.beginLatitude = {0.0, 10.0};
    grid.beginLongitude = {0.0, 10.0};
    grid.beginGrSrRatio = {1.0, 2.0};
    grid.midLatitude = {10.0, 20.0};
    grid.midLongitude = {10.0, 20.0};
    grid.midGrSrRatio = {2.0, 3.0};
    grid.endLatitude = {20.0, 30.0};
    grid.endLongitude = {20.0, 30.0};
    grid.endGrSrRatio = {3.0, 4.0};

    rpf::RpfStreamBlock block{};
    block.startLine = 1;
    block.numLines = 10;
    block.numPixels = 5;
    block.pixelType = 0;

    const auto stream = rpf::RpfProductStreamLine::makeSynthetic({block}, grid);
    double lat = 0.0;
    double lon = 0.0;
    double gr = 0.0;
    EXPECT_TRUE(stream.getLatLong(1, 1, lat, lon, gr));
}

TEST(RpfProductStreamLineTests, ClampsPixelOutsideGrid) {
    rpf::LatLongGrid grid{};
    grid.lineNumber = {1, 3};
    grid.beginLatitude = {0.0, 10.0};
    grid.beginLongitude = {0.0, 10.0};
    grid.beginGrSrRatio = {1.0, 2.0};
    grid.midLatitude = {10.0, 20.0};
    grid.midLongitude = {10.0, 20.0};
    grid.midGrSrRatio = {2.0, 3.0};
    grid.endLatitude = {20.0, 30.0};
    grid.endLongitude = {20.0, 30.0};
    grid.endGrSrRatio = {3.0, 4.0};

    rpf::RpfStreamBlock block{};
    block.startLine = 1;
    block.numLines = 3;
    block.numPixels = 5;
    block.pixelType = 0;

    const auto stream = rpf::RpfProductStreamLine::makeSynthetic({block}, grid);
    double lat = 0.0;
    double lon = 0.0;
    double gr = 0.0;
    EXPECT_TRUE(stream.getLatLong(1, 0, lat, lon, gr));
    EXPECT_TRUE(stream.getLatLong(1, 10, lat, lon, gr));
}
