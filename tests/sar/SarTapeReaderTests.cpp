#include <gtest/gtest.h>

#include "sar/SarTapeConstants.hpp"

TEST(SarTapeConstantsTests, RecordSizeIsNonZero) {
    EXPECT_GT(sar::SarTapeConstants::kRecordSize, 0u);
}
