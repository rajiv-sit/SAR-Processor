#include "pta/TtlUtils.hpp"

#include <algorithm>
#include <utility>

#include "pta/PtaAnalyzer.hpp"
#include "pta/PtaChip.hpp"

namespace pta {

namespace {

bool computeChipBounds(int center,
                       int halfSize,
                       int minIndex,
                       int maxIndex,
                       int& start,
                       int& end) {
    const int unclampedStart = center - halfSize;
    const int unclampedEnd = center + halfSize;
    start = std::max(minIndex, unclampedStart);
    end = std::min(maxIndex, unclampedEnd);
    return unclampedStart >= minIndex && unclampedEnd <= maxIndex;
}

Eigen::MatrixXf extractChip(const Eigen::MatrixXf& data,
                            int rowStart,
                            int rowEnd,
                            int colStart,
                            int colEnd) {
    const int rows = rowEnd - rowStart + 1;
    const int cols = colEnd - colStart + 1;
    Eigen::MatrixXf chip(rows, cols);
    for (int row = 0; row < rows; ++row) {
        for (int col = 0; col < cols; ++col) {
            chip(row, col) = data(rowStart + row, colStart + col);
        }
    }
    return chip;
}

void zeroChipRegion(Eigen::MatrixXf& data,
                    int rowStart,
                    int rowEnd,
                    int colStart,
                    int colEnd) {
    for (int row = rowStart; row <= rowEnd; ++row) {
        for (int col = colStart; col <= colEnd; ++col) {
            data(row, col) = 0.0f;
        }
    }
}

}  // namespace

std::vector<TtlAutoPeakResult> autoDetectPeaks2D(const Eigen::MatrixXf& data,
                                                 const TtlAutoPeakOptions& options) {
    std::vector<TtlAutoPeakResult> results;
    if (data.rows() == 0 || data.cols() == 0 || options.maxTargets == 0) {
        return results;
    }

    Eigen::MatrixXf scratch = data;
    const int halfRows = std::max(1, options.chipRows / 2);
    const int halfCols = std::max(1, options.chipCols / 2);
    float maxValue = 0.0f;

    PtaAnalyzer analyzer;
    for (std::size_t targetIndex = 0; targetIndex < options.maxTargets; ++targetIndex) {
        Eigen::Index peakRow = 0;
        Eigen::Index peakCol = 0;
        const float peakValue = scratch.maxCoeff(&peakRow, &peakCol);
        if (targetIndex == 0) {
            maxValue = peakValue;
        }
        if (options.thresholdFraction > 0.0f &&
            peakValue < maxValue * options.thresholdFraction) {
            break;
        }

        int rowStart = 0;
        int rowEnd = 0;
        int colStart = 0;
        int colEnd = 0;
        const bool inBounds =
            computeChipBounds(static_cast<int>(peakRow), halfRows, 0, data.rows() - 1, rowStart, rowEnd) &&
            computeChipBounds(static_cast<int>(peakCol), halfCols, 0, data.cols() - 1, colStart, colEnd);

        TtlAutoPeakResult result{};
        result.peakRow = static_cast<int>(peakRow);
        result.peakCol = static_cast<int>(peakCol);

        if (inBounds) {
            PtaChip chip{};
            chip.chipIn = extractChip(scratch, rowStart, rowEnd, colStart, colEnd);
            auto stats2d = analyzer.analyze2D(chip);
            result.xStats = stats2d.first;
            result.yStats = stats2d.second;
            result.valid = true;
        }

        if (rowStart <= rowEnd && colStart <= colEnd) {
            zeroChipRegion(scratch, rowStart, rowEnd, colStart, colEnd);
        }
        results.push_back(std::move(result));
    }

    return results;
}

}  // namespace pta
