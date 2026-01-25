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

TEST(FilterBankTests, HandlesZeroSizedWindow) {
    backproj::FilterParams params{};
    backproj::FilterBank bank(params, params);
    const auto window = bank.rangeWindow(0);
    EXPECT_EQ(window.size(), 0);
}

TEST(FilterBankTests, DefaultsWindowCoefWhenNonPositive) {
    backproj::FilterParams params{};
    params.windowCoef = 0.0;
    backproj::FilterBank bank(params, params);
    const auto window = bank.rangeWindow(1);
    ASSERT_EQ(window.size(), 1);
    EXPECT_FLOAT_EQ(window(0), 1.0f);
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

TEST(FilterBankTests, AppliesWindowAcrossRows) {
    backproj::FilterParams params{};
    backproj::FilterBank bank(params, params);
    Eigen::MatrixXf image(2, 1);
    image << 1.0f,
             2.0f;
    Eigen::VectorXf window(2);
    window << 2.0f, 3.0f;
    backproj::FilterBank::applyWindow(image, window, false);
    EXPECT_FLOAT_EQ(image(0, 0), 2.0f);
    EXPECT_FLOAT_EQ(image(1, 0), 6.0f);
}

TEST(FilterBankTests, IgnoresMismatchedWindow) {
    backproj::FilterParams params{};
    backproj::FilterBank bank(params, params);
    Eigen::MatrixXf image(1, 2);
    image << 1.0f, 2.0f;
    Eigen::VectorXf window(3);
    window << 1.0f, 2.0f, 3.0f;
    backproj::FilterBank::applyWindow(image, window, true);
    EXPECT_FLOAT_EQ(image(0, 0), 1.0f);
    EXPECT_FLOAT_EQ(image(0, 1), 2.0f);
}

TEST(FilterBankTests, HandlesEmptyImage) {
    backproj::FilterParams params{};
    backproj::FilterBank bank(params, params);
    Eigen::MatrixXf image;
    Eigen::VectorXf window(1);
    window << 1.0f;
    backproj::FilterBank::applyWindow(image, window, true);
    EXPECT_EQ(image.size(), 0);
}
