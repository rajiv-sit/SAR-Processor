#include "pta/PtaHistogram.hpp"

#include <algorithm>

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
    for (float value : values) {
        const double normalized = (static_cast<double>(value) - hist.minValue) / range;
        std::size_t index = static_cast<std::size_t>(normalized * static_cast<double>(bins));
        if (index >= bins) {
            index = bins - 1;
        }
        ++hist.counts[index];
    }

    return hist;
}

}  // namespace pta
