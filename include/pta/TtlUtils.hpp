#pragma once

#include <cstddef>
#include <vector>

#include <Eigen/Core>

#include "pta/PtaStats.hpp"

namespace pta {

struct TtlAutoPeakOptions {
    std::size_t maxTargets = 1;
    float thresholdFraction = 0.0f;
    int chipRows = 64;
    int chipCols = 64;
};

struct TtlAutoPeakResult {
    bool valid = false;
    int peakRow = 0;
    int peakCol = 0;
    PtaStats xStats{};
    PtaStats yStats{};
};

std::vector<TtlAutoPeakResult> autoDetectPeaks2D(const Eigen::MatrixXf& data,
                                                 const TtlAutoPeakOptions& options);

}  // namespace pta
