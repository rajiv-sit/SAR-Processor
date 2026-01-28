#pragma once

#include <utility>
#include <vector>

#include "pta/IPtaAnalyzer.hpp"
#include "pta/PtaChip.hpp"
#include "pta/PtaPeak.hpp"
#include "pta/PtaStats.hpp"

namespace pta {

class PtaAnalyzer : public IPtaAnalyzer {
public:
    using PtaAnalysisResult = IPtaAnalyzer::PtaAnalysisResult;

    PtaStats analyze1D(const PtaChip& chip) const override;
    std::pair<PtaStats, PtaStats> analyze2D(const PtaChip& chip) const override;
    std::pair<PtaStats, PtaStats> stats1DFrom2D(const PtaChip& chip) const override;
    std::vector<PtaPeak> findPeaks1D(const PtaChip& chip,
                                     std::size_t maxPeaks = 4,
                                     std::size_t minSeparation = 1) const override;
    PtaAnalysisResult analyze1DWithZoom(const PtaChip& chip,
                                        std::size_t fftSize = 0) const override;
};

}  // namespace pta
