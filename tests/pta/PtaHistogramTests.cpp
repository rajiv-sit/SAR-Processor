#include <gtest/gtest.h>

#include "pta/PtaHistogram.hpp"

TEST(PtaHistogramTests, GeneratesCounts) {
    const std::vector<float> values{0.0f, 0.5f, 1.0f, 1.0f};
    const auto hist = pta::generateHistogram(values, 2);
    ASSERT_EQ(hist.counts.size(), 2u);
    EXPECT_EQ(hist.counts[0], 2u);
    EXPECT_EQ(hist.counts[1], 2u);
}
