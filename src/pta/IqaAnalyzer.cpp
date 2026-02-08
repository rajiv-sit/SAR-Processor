#include "pta/IqaAnalyzer.hpp"

#include <algorithm>
#include <cmath>

#include "pta/PtaAnalyzer.hpp"
#include "pta/PtaChip.hpp"

namespace pta {

namespace {

std::vector<float> applyPowerDetection(const std::vector<float>& data,
                                       const std::string& mode) {
    if (mode == "power") {
        std::vector<float> output;
        output.reserve(data.size());
        for (float value : data) {
            output.push_back(value * value);
        }
        return output;
    }
    return data;
}

}  // namespace

IqaPeakResult IqaAnalyzer::process1DPeaks(const std::vector<float>& data,
                                          const IqaPeakOptions& options) const {
    IqaPeakResult result{};
    if (data.empty()) {
        return result;
    }

    const std::vector<float> processed = applyPowerDetection(data, options.powerDetection);

    PtaChip chip{};
    chip.magFactor = options.magFactor;
    chip.zpAlpha = options.zpAlpha;
    chip.powerDetection = options.powerDetection;
    chip.sideLobeMethod = options.sideLobeMethod;
    chip.chipIn.resize(1, static_cast<int>(processed.size()));
    for (std::size_t i = 0; i < processed.size(); ++i) {
        chip.chipIn(0, static_cast<int>(i)) = processed[i];
    }

    PtaAnalyzer analyzer;
    auto analysis = analyzer.analyze1DWithZoom(chip, options.fftSize);
    analysis.peaks = analyzer.findPeaks1D(chip, options.maxPeaks, options.minSeparation);

    result.stats = analysis.stats;
    result.peaks = std::move(analysis.peaks);
    result.zoomPower = std::move(analysis.zoomPower);
    return result;
}

IqaPeak2DResult IqaAnalyzer::process2DPeaks(const Eigen::MatrixXf& chip,
                                            const IqaPeakOptions& options) const {
    IqaPeak2DResult result{};
    if (chip.rows() == 0 || chip.cols() == 0) {
        return result;
    }

    std::vector<float> xProfile(static_cast<std::size_t>(chip.cols()), 0.0f);
    std::vector<float> yProfile(static_cast<std::size_t>(chip.rows()), 0.0f);
    for (int row = 0; row < chip.rows(); ++row) {
        for (int col = 0; col < chip.cols(); ++col) {
            const float value = chip(row, col);
            xProfile[static_cast<std::size_t>(col)] += value;
            yProfile[static_cast<std::size_t>(row)] += value;
        }
    }

    result.x = process1DPeaks(xProfile, options);
    result.y = process1DPeaks(yProfile, options);
    return result;
}

}  // namespace pta
