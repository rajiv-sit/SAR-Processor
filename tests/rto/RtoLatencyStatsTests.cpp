#include <gtest/gtest.h>

#include "rto/RtoLatencyStats.hpp"

TEST(RtoLatencyStatsTests, TracksMinMaxMean) {
    rto::RtoLatencyStats stats;
    stats.update(100, 110);
    stats.update(100, 120);
    stats.update(100, 130);

    const auto snapshot = stats.snapshot();
    EXPECT_EQ(snapshot.count, 3u);
    EXPECT_EQ(snapshot.minNs, 10u);
    EXPECT_EQ(snapshot.maxNs, 30u);
    EXPECT_NEAR(snapshot.meanNs, 20.0, 1e-6);
}

TEST(RtoLatencyStatsTests, ResetClearsStats) {
    rto::RtoLatencyStats stats;
    stats.update(10, 20);
    stats.reset();
    const auto snapshot = stats.snapshot();
    EXPECT_EQ(snapshot.count, 0u);
    EXPECT_EQ(snapshot.minNs, 0u);
    EXPECT_EQ(snapshot.maxNs, 0u);
    EXPECT_DOUBLE_EQ(snapshot.meanNs, 0.0);
}

TEST(RtoLatencyStatsTests, IgnoresNegativeLatency) {
    rto::RtoLatencyStats stats;
    stats.update(100, 90);
    const auto snapshot = stats.snapshot();
    EXPECT_EQ(snapshot.count, 0u);
}
