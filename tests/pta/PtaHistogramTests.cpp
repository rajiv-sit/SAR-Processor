#include <gtest/gtest.h>

#include "pta/PtaHistogram.hpp"

TEST(PtaHistogramTests, GeneratesCounts) {
    const std::vector<float> values{0.0f, 0.5f, 1.0f, 1.0f};
    const auto hist = pta::generateHistogram(values, 2);
    ASSERT_EQ(hist.counts.size(), 2u);
    EXPECT_EQ(hist.counts[0], 2u);
    EXPECT_EQ(hist.counts[1], 2u);
}

TEST(PtaHistogramTests, HandlesEmptyInput) {
    const auto hist = pta::generateHistogram({}, 4);
    EXPECT_TRUE(hist.counts.empty());
}

TEST(PtaHistogramTests, HandlesZeroBins) {
    std::vector<float> values{1.0f, 2.0f};
    const auto hist = pta::generateHistogram(values, 0);
    EXPECT_TRUE(hist.counts.empty());
}

TEST(PtaHistogramTests, HandlesSingleValue) {
    std::vector<float> values{5.0f, 5.0f, 5.0f};
    const auto hist = pta::generateHistogram(values, 4);
    ASSERT_EQ(hist.counts.size(), 4u);
    EXPECT_EQ(hist.counts[0], 3u);
}

TEST(PtaHistogramTests, ClampsOutOfRangeBins) {
    std::vector<float> values{0.0f, 1.0f, 100.0f};
    const auto hist = pta::generateHistogram(values, 2);
    ASSERT_EQ(hist.counts.size(), 2u);
    EXPECT_EQ(hist.counts[0], 2u);
    EXPECT_EQ(hist.counts[1], 1u);
}
