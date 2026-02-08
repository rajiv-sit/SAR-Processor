#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <Eigen/Core>

#include "pta/PtaPeak.hpp"
#include "pta/PtaStats.hpp"

namespace pta {

struct IqaPeakOptions {
    std::size_t maxPeaks = 4;
    std::size_t minSeparation = 1;
    std::size_t fftSize = 0;
    std::uint32_t magFactor = 1;
    double zpAlpha = 0.0;
    std::string powerDetection;
    std::string sideLobeMethod;
};

struct IqaPeakResult {
    PtaStats stats{};
    std::vector<PtaPeak> peaks;
    std::vector<float> zoomPower;
};

struct IqaPeak2DResult {
    IqaPeakResult x{};
    IqaPeakResult y{};
};

class IqaAnalyzer {
public:
    IqaPeakResult process1DPeaks(const std::vector<float>& data,
                                 const IqaPeakOptions& options) const;
    IqaPeak2DResult process2DPeaks(const Eigen::MatrixXf& chip,
                                   const IqaPeakOptions& options) const;
};

}  // namespace pta
