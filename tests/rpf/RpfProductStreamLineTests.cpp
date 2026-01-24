#include <gtest/gtest.h>

#include "rpf/RpfProductStreamLine.hpp"

TEST(RpfProductStreamLineTests, InterpolatesLatLongForLineAndPixel) {
    rpf::LatLongGrid grid{};
    grid.lineNumber = {1, 3};
    grid.beginLatitude = {0.0, 10.0};
    grid.beginLongitude = {0.0, 10.0};
    grid.beginGrSrRatio = {1.0, 2.0};
    grid.midLatitude = {10.0, 20.0};
    grid.midLongitude = {10.0, 20.0};
    grid.midGrSrRatio = {2.0, 3.0};
    grid.endLatitude = {20.0, 30.0};
    grid.endLongitude = {20.0, 30.0};
    grid.endGrSrRatio = {3.0, 4.0};

    rpf::RpfStreamBlock block{};
    block.startLine = 1;
    block.numLines = 3;
    block.numPixels = 5;
    block.pixelType = 0;

    const auto stream = rpf::RpfProductStreamLine::makeSynthetic({block}, grid);
    double lat = 0.0;
    double lon = 0.0;
    double gr = 0.0;
    ASSERT_TRUE(stream.getLatLong(2, 3, lat, lon, gr));
    EXPECT_DOUBLE_EQ(lat, 15.0);
    EXPECT_DOUBLE_EQ(lon, 15.0);
    EXPECT_DOUBLE_EQ(gr, 2.5);
}
