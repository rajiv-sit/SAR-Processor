#include "pta/PtaAnalyzer.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace pta {

namespace {

PtaStats analyzeVector(const std::vector<float>& data) {
    PtaStats stats{};
    if (data.empty()) {
        return stats;
    }

    const auto maxIter = std::max_element(data.begin(), data.end());
    const std::size_t maxIndex = static_cast<std::size_t>(std::distance(data.begin(), maxIter));
    const double maxValue = static_cast<double>(*maxIter);
    stats.maxPower = maxValue;
    stats.pos = static_cast<double>(maxIndex) + 1.0;

    const double halfPower = maxValue * 0.5;
    std::size_t left = maxIndex;
    std::size_t right = maxIndex;
    while (left > 0 && data[left] > halfPower) {
        --left;
    }
    while (right + 1 < data.size() && data[right] > halfPower) {
        ++right;
    }
    stats.irw = static_cast<double>(right - left);

    double sideLobeMax = 0.0;
    double sideLobeEnergy = 0.0;
    for (std::size_t i = 0; i < data.size(); ++i) {
        if (i >= left && i <= right) {
            continue;
        }
        sideLobeMax = std::max(sideLobeMax, static_cast<double>(data[i]));
        sideLobeEnergy += static_cast<double>(data[i]) * static_cast<double>(data[i]);
    }

    const double mainEnergy = maxValue * maxValue;
    stats.mslr = (sideLobeMax > 0.0) ? 20.0 * std::log10(sideLobeMax / maxValue) : 0.0;
    stats.islr = (sideLobeEnergy > 0.0) ? 10.0 * std::log10(sideLobeEnergy / mainEnergy) : 0.0;
    return stats;
}

}  // namespace

PtaStats PtaAnalyzer::analyze1D(const PtaChip& chip) const {
    std::vector<float> data;
    data.reserve(static_cast<std::size_t>(chip.chipIn.size()));
    for (int i = 0; i < chip.chipIn.size(); ++i) {
        data.push_back(chip.chipIn(i));
    }
    return analyzeVector(data);
}

std::pair<PtaStats, PtaStats> PtaAnalyzer::analyze2D(const PtaChip& chip) const {
    if (chip.chipIn.rows() == 0 || chip.chipIn.cols() == 0) {
        return {PtaStats{}, PtaStats{}};
    }

    std::vector<float> xProfile(chip.chipIn.cols(), 0.0f);
    std::vector<float> yProfile(chip.chipIn.rows(), 0.0f);

    for (int row = 0; row < chip.chipIn.rows(); ++row) {
        for (int col = 0; col < chip.chipIn.cols(); ++col) {
            const float value = chip.chipIn(row, col);
            xProfile[col] += value;
            yProfile[row] += value;
        }
    }

    return {analyzeVector(xProfile), analyzeVector(yProfile)};
}

}  // namespace pta
