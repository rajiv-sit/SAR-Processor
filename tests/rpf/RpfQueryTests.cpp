#include <filesystem>

#include <gtest/gtest.h>

#include <Eigen/Core>

#include "rpf/RpfConstants.hpp"
#include "rpf/RpfQuery.hpp"
#include "rpf/RpfWriter.hpp"

TEST(RpfQueryTests, ReturnsFalseWhenFileMissing) {
    rpf::RpfQueryResult result{};
    EXPECT_FALSE(rpf::queryRpfFile("missing.rpf", result));
}

TEST(RpfQueryTests, ReadsFrameSeqForSpotMode) {
    Eigen::MatrixXf image(1, 2);
    image << 1.0f, 2.0f;
    rpf::RpfWriteOptions options{};
    options.pixelType = 0;
    options.radarMode = rpf::RpfConstants::kLandspotMode;
    options.frameSeqNum = 7;
    const auto path = std::filesystem::temp_directory_path() / "rpf_query_spot.rpf";
    std::string error;
    ASSERT_TRUE(rpf::writeRpfFile(path.string(), image, options, error)) << error;

    rpf::RpfQueryResult result{};
    ASSERT_TRUE(rpf::queryRpfFile(path.string(), result));
    ASSERT_EQ(result.startNums.size(), 1u);
    EXPECT_EQ(result.startNums.front(), 7);
    EXPECT_EQ(result.numPixels.front(), 2u);
}

TEST(RpfQueryTests, ReadsStripmapStartLine) {
    Eigen::MatrixXf image(2, 2);
    image << 1.0f, 2.0f,
             3.0f, 4.0f;
    rpf::RpfWriteOptions options{};
    options.pixelType = 0;
    options.radarMode = rpf::RpfConstants::kStripmapMode;
    options.geolocationGridNumLines = 2;
    const auto path = std::filesystem::temp_directory_path() / "rpf_query_strip.rpf";
    std::string error;
    ASSERT_TRUE(rpf::writeRpfFile(path.string(), image, options, error)) << error;

    rpf::RpfQueryResult result{};
    ASSERT_TRUE(rpf::queryRpfFile(path.string(), result));
    ASSERT_EQ(result.startNums.size(), 1u);
    EXPECT_EQ(result.mode, rpf::RpfConstants::kStripmapMode);
    EXPECT_EQ(result.numLines.front(), 2u);
}
