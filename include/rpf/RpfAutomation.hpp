#pragma once

#include <string>
#include <vector>

#include "pta/PtaStats.hpp"
#include "rpf/AnnotationStruct.hpp"
#include "rpf/LatLongGrid.hpp"

namespace rpf {

class RpfAutomation {
public:
    struct AutoPtaEntry {
        int line = 0;
        pta::PtaStats xStats{};
        pta::PtaStats yStats{};
    };

    bool writeAnnotationReport(const std::string& path,
                               const AnnotationStruct& annotation,
                               const LatLongGrid& grid,
                               const pta::PtaStats& xStats,
                               const pta::PtaStats& yStats) const;

    bool writeAutoPtaReport(const std::string& path,
                            const AnnotationStruct& annotation,
                            const std::vector<AutoPtaEntry>& entries) const;

    bool writeStripmapLinesToReprocess(const std::string& path,
                                       const std::vector<int>& lines) const;
};

}  // namespace rpf
