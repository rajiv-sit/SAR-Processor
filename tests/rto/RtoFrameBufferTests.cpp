#include <gtest/gtest.h>
#include <stdexcept>

#include "rto/RtoDataBus.hpp"
#include "rto/RtoFrameBuffer.hpp"

TEST(RtoFrameBufferTests, DropsOldestWhenCapacityExceeded) {
    rto::RtoFrameBuffer buffer(2);

    rto::RtoFrame first{};
    first.timestampNs = 1;
    rto::RtoFrame second{};
    second.timestampNs = 2;
    rto::RtoFrame third{};
    third.timestampNs = 3;

    buffer.push(first);
    buffer.push(second);
    buffer.push(third);

    EXPECT_EQ(buffer.size(), 2u);

    rto::RtoFrame out{};
    ASSERT_TRUE(buffer.pop(out));
    EXPECT_EQ(out.timestampNs, 2u);
    ASSERT_TRUE(buffer.pop(out));
    EXPECT_EQ(out.timestampNs, 3u);
    EXPECT_FALSE(buffer.pop(out));
}

TEST(RtoDataBusTests, PublishReturnsTrue) {
    rto::RtoDataBus bus("udp://127.0.0.1:5000");
    rto::RtoFrame frame{};
    frame.width = 2;
    frame.height = 2;
    frame.timestampNs = 100;
    frame.pixels = {0.0f, 1.0f, 2.0f, 3.0f};
    EXPECT_TRUE(bus.publish(frame));
}

TEST(RtoDataBusTests, PublishReturnsFalseOnInvalidEndpoint) {
    rto::RtoDataBus bus("udp://");
    rto::RtoFrame frame{};
    frame.width = 1;
    frame.height = 1;
    frame.pixels = {1.0f};
    EXPECT_FALSE(bus.publish(frame));
}

TEST(RtoDataBusTests, PublishReturnsFalseOnInvalidHost) {
    rto::RtoDataBus bus("udp://not_an_ip:4000");
    rto::RtoFrame frame{};
    frame.width = 1;
    frame.height = 1;
    frame.pixels = {1.0f};
    EXPECT_FALSE(bus.publish(frame));
}

TEST(RtoDataBusTests, PublishWorksWithoutUdpPrefix) {
    rto::RtoDataBus bus("127.0.0.1:5001");
    rto::RtoFrame frame{};
    frame.width = 1;
    frame.height = 1;
    frame.pixels = {1.0f};
    EXPECT_TRUE(bus.publish(frame));
}
TEST(RtoFrameBufferTests, RejectsZeroCapacity) {
    EXPECT_THROW(rto::RtoFrameBuffer(0), std::invalid_argument);
}
