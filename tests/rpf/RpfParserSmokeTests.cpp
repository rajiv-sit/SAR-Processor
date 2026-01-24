#include <gtest/gtest.h>

#include "rpf/AnnotationStruct.hpp"
#include "rpf/LatLongGrid.hpp"
#include "rpf/RpfProductStream.hpp"

TEST(RpfParserSmokeTests, MissingFileReturnsFalse) {
    rpf::RpfProductStream stream("missing.rpf");
    rpf::AnnotationStruct annotation{};
    rpf::LatLongGrid grid{};

    EXPECT_FALSE(stream.nextBlock(annotation, grid, true));
}
