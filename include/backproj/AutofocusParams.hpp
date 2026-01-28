#pragma once

#include <cstddef>
#include <string>

namespace backproj {

struct AutofocusParams {
    std::string selectionMethod = "pga";
    double minMetricDelta = 0.0;
    std::size_t maxPoints = 16;
    std::size_t maxFrames = 0;
};

}  // namespace backproj
