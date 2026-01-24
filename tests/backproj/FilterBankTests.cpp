#include <gtest/gtest.h>

#include "backproj/FilterBank.hpp"

TEST(FilterBankTests, BuildsHammingWindow) {
    backproj::FilterParams params{};
    params.windowCoef = 0.54;
    backproj::FilterBank bank(params, params);

    const auto window = bank.rangeWindow(4);
    ASSERT_EQ(window.size(), 4);
    EXPECT_GT(window(1), 0.0f);
}

TEST(FilterBankTests, AppliesWindowAcrossColumns) {
    backproj::FilterParams params{};
    backproj::FilterBank bank(params, params);
    Eigen::MatrixXf image(1, 2);
    image << 1.0f, 2.0f;
    Eigen::VectorXf window(2);
    window << 2.0f, 3.0f;
    backproj::FilterBank::applyWindow(image, window, true);
    EXPECT_FLOAT_EQ(image(0, 0), 2.0f);
    EXPECT_FLOAT_EQ(image(0, 1), 6.0f);
}
