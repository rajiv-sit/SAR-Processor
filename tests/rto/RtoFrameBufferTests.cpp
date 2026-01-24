#include <gtest/gtest.h>

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
    rto::RtoDataBus bus("tcp://127.0.0.1:5555");
    rto::RtoFrame frame{};
    EXPECT_TRUE(bus.publish(frame));
}
