#include <gtest/gtest.h>

#include "pta/PtaAnalyzer.hpp"
#include "pta/PtaChip.hpp"

TEST(PtaAnalyzerTests, Analyze1DHandlesSinglePeak) {
    pta::PtaChip chip;
    chip.chipIn.resize(1, 3);
    chip.chipIn << 0.0f, 1.0f, 0.0f;

    pta::PtaAnalyzer analyzer;
    const auto stats = analyzer.analyze1D(chip);
    EXPECT_DOUBLE_EQ(stats.maxPower, 1.0);
    EXPECT_DOUBLE_EQ(stats.pos, 2.0);
    EXPECT_DOUBLE_EQ(stats.irw, 2.0);
    EXPECT_DOUBLE_EQ(stats.mslr, 0.0);
    EXPECT_DOUBLE_EQ(stats.islr, 0.0);
}

TEST(PtaAnalyzerTests, Analyze2DReturnsProfiles) {
    pta::PtaChip chip;
    chip.chipIn.resize(2, 2);
    chip.chipIn << 1.0f, 2.0f,
                   3.0f, 4.0f;

    pta::PtaAnalyzer analyzer;
    const auto stats = analyzer.analyze2D(chip);
    EXPECT_DOUBLE_EQ(stats.first.maxPower, 6.0);
    EXPECT_DOUBLE_EQ(stats.second.maxPower, 7.0);
}

TEST(PtaAnalyzerTests, Analyze2DHandlesEmptyInput) {
    pta::PtaChip chip;
    chip.chipIn.resize(0, 0);

    pta::PtaAnalyzer analyzer;
    const auto stats = analyzer.analyze2D(chip);
    EXPECT_DOUBLE_EQ(stats.first.maxPower, 0.0);
    EXPECT_DOUBLE_EQ(stats.second.maxPower, 0.0);
}
