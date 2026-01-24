#pragma once

#include <cstdint>
#include <string>

namespace sar {

struct SarSceneParams {
    double carrierFreq = 0.0;
    double lambda = 0.0;
    double pulseLen = 0.0;
    double sampRate = 0.0;
    double linFMRate = 0.0;
    double velocity = 0.0;
    double rangeSc = 0.0;
    double etaSc = 0.0;
    double prf = 0.0;
    std::uint32_t numLines = 0;
    std::uint32_t numRngSamp = 0;
    std::string dataType;
};

}  // namespace sar
