#include "pta/PtaHistogram.hpp"

#include <algorithm>
#include <cmath>

namespace pta {

PtaHistogram generateHistogram(const std::vector<float>& values, std::size_t bins) {
    PtaHistogram hist{};
    if (values.empty() || bins == 0) {
        return hist;
    }
    auto minmax = std::minmax_element(values.begin(), values.end());
    hist.minValue = static_cast<double>(*minmax.first);
    hist.maxValue = static_cast<double>(*minmax.second);
    hist.counts.assign(bins, 0);
    if (hist.minValue == hist.maxValue) {
        hist.counts[0] = values.size();
        return hist;
    }

    const double range = hist.maxValue - hist.minValue;
    const double binWidth = range / static_cast<double>(bins);
    for (float value : values) {
        std::size_t index = 0;
        const double val = static_cast<double>(value);
        if (val <= hist.minValue) {
            index = 0;
        } else if (val >= hist.maxValue) {
            index = bins - 1;
        } else if (binWidth > 0.0) {
            const double normalized = (val - hist.minValue) / binWidth;
            index = static_cast<std::size_t>(std::ceil(normalized)) - 1;
            if (index >= bins) index = bins - 1;
        }
        ++hist.counts[index];
    }

    return hist;
}

}  // namespace pta
