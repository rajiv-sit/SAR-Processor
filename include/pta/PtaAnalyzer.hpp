#pragma once

#include <utility>
#include <vector>

#include "pta/PtaChip.hpp"
#include "pta/PtaPeak.hpp"
#include "pta/PtaStats.hpp"

namespace pta {

class PtaAnalyzer {
public:
    struct PtaAnalysisResult {
        PtaStats stats{};
        std::vector<float> zoomPower;
        std::vector<PtaPeak> peaks;
    };

    PtaStats analyze1D(const PtaChip& chip) const;
    std::pair<PtaStats, PtaStats> analyze2D(const PtaChip& chip) const;
    std::pair<PtaStats, PtaStats> stats1DFrom2D(const PtaChip& chip) const;
    std::vector<PtaPeak> findPeaks1D(const PtaChip& chip,
                                     std::size_t maxPeaks = 4,
                                     std::size_t minSeparation = 1) const;
    PtaAnalysisResult analyze1DWithZoom(const PtaChip& chip,
                                        std::size_t fftSize = 0) const;
};

}  // namespace pta
