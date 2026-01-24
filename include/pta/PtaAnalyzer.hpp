#pragma once

#include <utility>

#include "pta/PtaChip.hpp"
#include "pta/PtaStats.hpp"

namespace pta {

class PtaAnalyzer {
public:
    PtaStats analyze1D(const PtaChip& chip) const;
    std::pair<PtaStats, PtaStats> analyze2D(const PtaChip& chip) const;
};

}  // namespace pta
