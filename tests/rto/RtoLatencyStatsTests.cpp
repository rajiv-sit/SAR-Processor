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
