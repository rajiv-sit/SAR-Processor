#include <gtest/gtest.h>

#include <Eigen/Core>

#include "pta/TtlUtils.hpp"

TEST(TtlUtilsTests, AutoDetectPeaks2DFindsTopTargets) {
    Eigen::MatrixXf data(5, 5);
    data.setZero();
    data(1, 1) = 10.0f;
    data(3, 3) = 8.0f;

    pta::TtlAutoPeakOptions options{};
    options.maxTargets = 2;
    options.chipRows = 3;
    options.chipCols = 3;

    const auto results = pta::autoDetectPeaks2D(data, options);
    ASSERT_EQ(results.size(), 2u);
    EXPECT_TRUE(results[0].valid);
    EXPECT_EQ(results[0].peakRow, 1);
    EXPECT_EQ(results[0].peakCol, 1);
    EXPECT_TRUE(results[1].valid);
    EXPECT_EQ(results[1].peakRow, 3);
    EXPECT_EQ(results[1].peakCol, 3);
}
