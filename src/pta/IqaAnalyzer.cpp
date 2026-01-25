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

}  // namespace pta
