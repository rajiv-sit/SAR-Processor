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

TEST(PtaAnalyzerTests, Stats1DFrom2DMatchesAnalyze2D) {
    pta::PtaChip chip;
    chip.chipIn.resize(2, 3);
    chip.chipIn << 1.0f, 1.0f, 1.0f,
                   0.0f, 2.0f, 0.0f;

    pta::PtaAnalyzer analyzer;
    const auto statsA = analyzer.analyze2D(chip);
    const auto statsB = analyzer.stats1DFrom2D(chip);
    EXPECT_DOUBLE_EQ(statsA.first.maxPower, statsB.first.maxPower);
    EXPECT_DOUBLE_EQ(statsA.second.maxPower, statsB.second.maxPower);
}

TEST(PtaAnalyzerTests, FindPeaksReturnsTopPeaks) {
    pta::PtaChip chip;
    chip.chipIn.resize(1, 7);
    chip.chipIn << 0.0f, 1.0f, 0.0f, 2.0f, 0.0f, 1.5f, 0.0f;

    pta::PtaAnalyzer analyzer;
    const auto peaks = analyzer.findPeaks1D(chip, 2, 1);
    ASSERT_EQ(peaks.size(), 2u);
    EXPECT_EQ(peaks[0].index, 4u);
    EXPECT_DOUBLE_EQ(peaks[0].power, 2.0);
}

TEST(PtaAnalyzerTests, Analyze1DHandlesEmptyInput) {
    pta::PtaChip chip;
    chip.chipIn.resize(0, 0);

    pta::PtaAnalyzer analyzer;
    const auto stats = analyzer.analyze1D(chip);
    EXPECT_DOUBLE_EQ(stats.maxPower, 0.0);
    EXPECT_DOUBLE_EQ(stats.pos, 0.0);
}

TEST(PtaAnalyzerTests, Analyze1DReportsSideLobes) {
    pta::PtaChip chip;
    chip.chipIn.resize(1, 5);
    chip.chipIn << 1.0f, 0.5f, 2.0f, 0.5f, 0.2f;

    pta::PtaAnalyzer analyzer;
    const auto stats = analyzer.analyze1D(chip);
    EXPECT_LT(stats.mslr, 0.0);
    EXPECT_LT(stats.islr, 0.0);
}

TEST(PtaAnalyzerTests, FindPeaksHandlesSmallInput) {
    pta::PtaChip chip;
    chip.chipIn.resize(1, 2);
    chip.chipIn << 1.0f, 0.0f;

    pta::PtaAnalyzer analyzer;
    const auto peaks = analyzer.findPeaks1D(chip, 2, 1);
    EXPECT_TRUE(peaks.empty());
}

TEST(PtaAnalyzerTests, FindPeaksFiltersBySeparationFor2D) {
    pta::PtaChip chip;
    chip.chipIn.resize(2, 5);
    chip.chipIn << 0.0f, 2.0f, 0.0f, 1.5f, 0.0f,
                   0.0f, 2.0f, 0.0f, 1.0f, 0.0f;

    pta::PtaAnalyzer analyzer;
    const auto peaks = analyzer.findPeaks1D(chip, 2, 3);
    ASSERT_EQ(peaks.size(), 1u);
    EXPECT_EQ(peaks[0].index, 2u);
}

TEST(PtaAnalyzerTests, FindPeaksReturnsEmptyWhenMaxPeaksZero) {
    pta::PtaChip chip;
    chip.chipIn.resize(1, 5);
    chip.chipIn << 0.0f, 1.0f, 0.0f, 2.0f, 0.0f;

    pta::PtaAnalyzer analyzer;
    const auto peaks = analyzer.findPeaks1D(chip, 0, 1);
    EXPECT_TRUE(peaks.empty());
}

TEST(PtaAnalyzerTests, FindPeaksSkipsTooClosePeaks) {
    pta::PtaChip chip;
    chip.chipIn.resize(1, 6);
    chip.chipIn << 0.0f, 3.0f, 0.0f, 2.5f, 0.0f, 1.0f;

    pta::PtaAnalyzer analyzer;
    const auto peaks = analyzer.findPeaks1D(chip, 3, 3);
    ASSERT_EQ(peaks.size(), 1u);
    EXPECT_EQ(peaks[0].index, 2u);
}

TEST(PtaAnalyzerTests, Analyze1DWithZoomUpsamples) {
    pta::PtaChip chip;
    chip.magFactor = 4;
    chip.chipIn.resize(1, 4);
    chip.chipIn << 0.0f, 1.0f, 0.0f, 0.5f;

    pta::PtaAnalyzer analyzer;
    const auto result = analyzer.analyze1DWithZoom(chip);
    EXPECT_GT(result.zoomPower.size(), 4u);
    EXPECT_GT(result.stats.maxPower, 0.0);
    EXPECT_FALSE(result.peaks.empty());
}
