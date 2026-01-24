#pragma once

#include <cstddef>
#include <vector>

namespace pta {

struct PtaHistogram {
    std::vector<std::size_t> counts;
    double minValue = 0.0;
    double maxValue = 0.0;
};

PtaHistogram generateHistogram(const std::vector<float>& values, std::size_t bins);

}  // namespace pta
