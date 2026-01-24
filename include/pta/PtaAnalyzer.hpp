#pragma once

#include <utility>
#include <vector>

#include "pta/PtaChip.hpp"
#include "pta/PtaPeak.hpp"
#include "pta/PtaStats.hpp"

namespace pta {

class PtaAnalyzer {
public:
    PtaStats analyze1D(const PtaChip& chip) const;
    std::pair<PtaStats, PtaStats> analyze2D(const PtaChip& chip) const;
    std::pair<PtaStats, PtaStats> stats1DFrom2D(const PtaChip& chip) const;
    std::vector<PtaPeak> findPeaks1D(const PtaChip& chip,
                                     std::size_t maxPeaks = 4,
                                     std::size_t minSeparation = 1) const;
};

}  // namespace pta
