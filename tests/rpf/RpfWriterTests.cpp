#include <cmath>
#include <filesystem>
#include <limits>

#include <gtest/gtest.h>

#include <Eigen/Core>

#include "rpf/LatLongGrid.hpp"
#include "rpf/RpfWriter.hpp"

TEST(RpfWriterTests, RejectsEmptyImage) {
    Eigen::MatrixXf image;
    rpf::RpfWriteOptions options{};
    std::string error;
    EXPECT_FALSE(rpf::writeRpfFile("empty.rpf", image, options, error));
    EXPECT_FALSE(error.empty());
}

TEST(RpfWriterTests, RejectsInvalidGridLines) {
    Eigen::MatrixXf image(1, 1);
    image << 1.0f;
    rpf::RpfWriteOptions options{};
    options.geolocationGridNumLines = 1;
    std::string error;
    EXPECT_FALSE(rpf::writeRpfFile("invalid_grid.rpf", image, options, error));
    EXPECT_FALSE(error.empty());
}

TEST(RpfWriterTests, RejectsInvalidOutputPath) {
    Eigen::MatrixXf image(1, 1);
    image << 1.0f;
    rpf::RpfWriteOptions options{};
    std::string error;
    const auto path = std::filesystem::temp_directory_path() / "no_such_dir" / "file.rpf";
    EXPECT_FALSE(rpf::writeRpfFile(path.string(), image, options, error));
    EXPECT_FALSE(error.empty());
}

TEST(RpfWriterTests, WritesHalfWithSpecialValues) {
    Eigen::MatrixXf image(1, 3);
    image << std::numeric_limits<float>::quiet_NaN(),
             std::numeric_limits<float>::infinity(),
             1e10f;
    rpf::RpfWriteOptions options{};
    options.pixelType = 4;
    options.radarMode = 1;
    const auto path = std::filesystem::temp_directory_path() / "rpf_half_special.rpf";
    std::string error;
    ASSERT_TRUE(rpf::writeRpfFile(path.string(), image, options, error)) << error;
    EXPECT_TRUE(std::filesystem::exists(path));
}

TEST(RpfWriterTests, WritesHalfWithSubnormalValue) {
    Eigen::MatrixXf image(1, 1);
    image << 1e-6f;
    rpf::RpfWriteOptions options{};
    options.pixelType = 4;
    options.radarMode = 1;
    const auto path = std::filesystem::temp_directory_path() / "rpf_half_subnormal.rpf";
    std::string error;
    ASSERT_TRUE(rpf::writeRpfFile(path.string(), image, options, error)) << error;
    EXPECT_TRUE(std::filesystem::exists(path));
}

TEST(RpfWriterTests, WritesGeoGridWhenProvided) {
    Eigen::MatrixXf image(2, 2);
    image << 1.0f, 2.0f,
             3.0f, 4.0f;
    rpf::RpfWriteOptions options{};
    options.geolocationGridNumLines = 2;
    rpf::LatLongGrid grid{};
    grid.lineNumber = {1, 2};
    grid.beginGrSrRatio = {1.0, 1.0};
    grid.midGrSrRatio = {1.0, 1.0};
    grid.endGrSrRatio = {1.0, 1.0};
    grid.beginLatitude = {10.0, 11.0};
    grid.beginLongitude = {20.0, 21.0};
    grid.midLatitude = {10.5, 11.5};
    grid.midLongitude = {20.5, 21.5};
    grid.endLatitude = {11.0, 12.0};
    grid.endLongitude = {21.0, 22.0};

    const auto path = std::filesystem::temp_directory_path() / "rpf_writer_grid.rpf";
    std::string error;
    ASSERT_TRUE(rpf::writeRpfFile(path.string(), image, options, grid, error)) << error;
    EXPECT_TRUE(std::filesystem::exists(path));
}
