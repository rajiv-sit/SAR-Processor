#pragma once

#include <cstdint>
#include <string>

#include <Eigen/Core>

namespace pta {

struct PtaChip {
    Eigen::MatrixXf chipIn;
    std::uint32_t magFactor = 1;
    double zpAlpha = 0.0;
    std::string powerDetection;
    std::string sideLobeMethod;
};

}  // namespace pta
