#include <gtest/gtest.h>

#include "rto/RtoDataBus.hpp"

TEST(RtoDataBusTests, PublishFailsWithMissingHost) {
    rto::RtoDataBus bus("udp://:1234");
    rto::RtoFrame frame{};
    frame.width = 1;
    frame.height = 1;
    frame.timestampNs = 1;
    frame.pixels = {1.0f};
    EXPECT_FALSE(bus.publish(frame));
}

TEST(RtoDataBusTests, PublishFailsWithZeroDimensions) {
    rto::RtoDataBus bus("udp://127.0.0.1:1234");
    rto::RtoFrame frame{};
    frame.width = 0;
    frame.height = 1;
    frame.timestampNs = 1;
    EXPECT_FALSE(bus.publish(frame));
}

TEST(RtoDataBusTests, PublishFailsWithInvalidHost) {
    rto::RtoDataBus bus("udp://invalid_host:1234");
    rto::RtoFrame frame{};
    frame.width = 1;
    frame.height = 1;
    frame.timestampNs = 1;
    frame.pixels = {1.0f, 2.0f};
    EXPECT_FALSE(bus.publish(frame));
}

TEST(RtoDataBusTests, PublishSendsPayload) {
    rto::RtoDataBus bus("127.0.0.1:9999");
    rto::RtoFrame frame{};
    frame.width = 1;
    frame.height = 1;
    frame.timestampNs = 123;
    frame.pixels = {2.5f};
    EXPECT_TRUE(bus.publish(frame));
}
