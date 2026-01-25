#include <cmath>
#include <filesystem>
#include <limits>

#include <gtest/gtest.h>

#include <Eigen/Core>

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
