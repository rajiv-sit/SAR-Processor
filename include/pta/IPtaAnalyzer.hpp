#pragma once

#include <utility>
#include <vector>

#include "pta/PtaChip.hpp"
#include "pta/PtaPeak.hpp"
#include "pta/PtaStats.hpp"

namespace pta {

class IPtaAnalyzer {
public:
    struct PtaAnalysisResult {
        PtaStats stats{};
        std::vector<float> zoomPower;
        std::vector<PtaPeak> peaks;
    };

    virtual ~IPtaAnalyzer() = default;

    virtual PtaStats analyze1D(const PtaChip& chip) const = 0;
    virtual std::pair<PtaStats, PtaStats> analyze2D(const PtaChip& chip) const = 0;
    virtual std::pair<PtaStats, PtaStats> stats1DFrom2D(const PtaChip& chip) const = 0;
    virtual std::vector<PtaPeak> findPeaks1D(const PtaChip& chip,
                                             std::size_t maxPeaks,
                                             std::size_t minSeparation) const = 0;
    virtual PtaAnalysisResult analyze1DWithZoom(const PtaChip& chip,
                                                std::size_t fftSize) const = 0;
};

}  // namespace pta
