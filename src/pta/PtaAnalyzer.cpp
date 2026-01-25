#include "pta/PtaAnalyzer.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <vector>

#include <unsupported/Eigen/FFT>

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

std::vector<float> extractProfile(const PtaChip& chip) {
    std::vector<float> data;
    if (chip.chipIn.rows() == 0 || chip.chipIn.cols() == 0) {
        return data;
    }
    if (chip.chipIn.rows() == 1 || chip.chipIn.cols() == 1) {
        data.reserve(static_cast<std::size_t>(chip.chipIn.size()));
        for (int i = 0; i < chip.chipIn.size(); ++i) {
            data.push_back(chip.chipIn(i));
        }
    } else {
        data.assign(static_cast<std::size_t>(chip.chipIn.cols()), 0.0f);
        for (int row = 0; row < chip.chipIn.rows(); ++row) {
            for (int col = 0; col < chip.chipIn.cols(); ++col) {
                data[static_cast<std::size_t>(col)] += chip.chipIn(row, col);
            }
        }
    }
    return data;
}

std::vector<float> zoomByFft(const std::vector<float>& data, std::size_t fftSize) {
    if (data.empty()) {
        return {};
    }
    const std::size_t baseSize = data.size();
    if (fftSize == 0 || fftSize <= baseSize) {
        return data;
    }

    Eigen::FFT<float> fft;
    std::vector<std::complex<float>> time(baseSize);
    for (std::size_t i = 0; i < baseSize; ++i) {
        time[i] = std::complex<float>(data[i], 0.0f);
    }

    std::vector<std::complex<float>> freq(baseSize);
    fft.fwd(freq, time);

    std::vector<std::complex<float>> padded(fftSize, std::complex<float>(0.0f, 0.0f));
    const std::size_t half = baseSize / 2;
    for (std::size_t i = 0; i <= half; ++i) {
        padded[i] = freq[i];
    }
    for (std::size_t i = half + 1; i < baseSize; ++i) {
        padded[fftSize - (baseSize - i)] = freq[i];
    }

    std::vector<std::complex<float>> zoomed(fftSize);
    fft.inv(zoomed, padded);

    std::vector<float> power(fftSize, 0.0f);
    for (std::size_t i = 0; i < fftSize; ++i) {
        power[i] = std::abs(zoomed[i]);
    }
    return power;
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

std::pair<PtaStats, PtaStats> PtaAnalyzer::stats1DFrom2D(const PtaChip& chip) const {
    return analyze2D(chip);
}

std::vector<PtaPeak> PtaAnalyzer::findPeaks1D(const PtaChip& chip,
                                              std::size_t maxPeaks,
                                              std::size_t minSeparation) const {
    std::vector<float> data = extractProfile(chip);

    std::vector<PtaPeak> peaks;
    if (data.size() < 3 || maxPeaks == 0) {
        return peaks;
    }

    for (std::size_t i = 1; i + 1 < data.size(); ++i) {
        if (data[i] >= data[i - 1] && data[i] >= data[i + 1]) {
            peaks.push_back({i + 1, static_cast<double>(data[i])});
        }
    }

    std::sort(peaks.begin(), peaks.end(),
              [](const PtaPeak& a, const PtaPeak& b) { return a.power > b.power; });

    std::vector<PtaPeak> filtered;
    for (const auto& peak : peaks) {
        bool tooClose = false;
        for (const auto& existing : filtered) {
            const std::size_t diff = (peak.index > existing.index)
                                         ? (peak.index - existing.index)
                                         : (existing.index - peak.index);
            if (diff < minSeparation) {
                tooClose = true;
                break;
            }
        }
        if (!tooClose) {
            filtered.push_back(peak);
        }
        if (filtered.size() >= maxPeaks) {
            break;
        }
    }

    return filtered;
}

PtaAnalyzer::PtaAnalysisResult PtaAnalyzer::analyze1DWithZoom(const PtaChip& chip,
                                                              std::size_t fftSize) const {
    PtaAnalysisResult result{};
    std::vector<float> profile = extractProfile(chip);
    if (profile.empty()) {
        return result;
    }

    std::size_t targetSize = fftSize;
    if (targetSize == 0) {
        if (chip.magFactor > 1) {
            targetSize = profile.size() * static_cast<std::size_t>(chip.magFactor);
        } else {
            targetSize = profile.size();
        }
    }

    result.zoomPower = zoomByFft(profile, targetSize);
    result.stats = analyzeVector(result.zoomPower);
    result.peaks = findPeaks1D(chip);
    return result;
}

}  // namespace pta
