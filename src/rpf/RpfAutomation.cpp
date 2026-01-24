#include "rpf/RpfAutomation.hpp"

#include <fstream>

#include <nlohmann/json.hpp>

namespace rpf {

namespace {

nlohmann::json toJson(const ImageDataChunkHeader& header) {
    return {
        {"dataWidth", header.dataWidth},
        {"dataHeight", header.dataHeight},
        {"frameSeqNum", header.frameSeqNum},
        {"pixelType", header.pixelType},
        {"rspInhibit", header.rspInhibit},
        {"pixelMarginStart", header.pixelMarginStart},
        {"pixelMarginEnd", header.pixelMarginEnd},
        {"lineMarginStart", header.lineMarginStart},
        {"lineMarginEnd", header.lineMarginEnd}
    };
}

nlohmann::json toJson(const pta::PtaStats& stats) {
    return {
        {"irw", stats.irw},
        {"mslr", stats.mslr},
        {"islr", stats.islr},
        {"pos", stats.pos},
        {"maxPower", stats.maxPower}
    };
}

}  // namespace

bool RpfAutomation::writeAnnotationReport(const std::string& path,
                                          const AnnotationStruct& annotation,
                                          const LatLongGrid& grid,
                                          const pta::PtaStats& xStats,
                                          const pta::PtaStats& yStats) const {
    nlohmann::json report{
        {"fileName", annotation.fileName},
        {"radarMode", annotation.fileIdParams.radarMode},
        {"geolocationGridNumLines", annotation.latLongOutput.geolocationGridNumLines},
        {"imageDataChunkHeader", toJson(annotation.imageDataChunkHeader)},
        {"latLongGridCount", grid.lineNumber.size()},
        {"ptaStats", {{"x", toJson(xStats)}, {"y", toJson(yStats)}}}
    };

    std::ofstream output(path);
    if (!output) {
        return false;
    }
    output << report.dump(2) << '\n';
    return true;
}

bool RpfAutomation::writeStripmapLinesToReprocess(const std::string& path,
                                                  const std::vector<int>& lines) const {
    nlohmann::json report{{"lines", lines}};
    std::ofstream output(path);
    if (!output) {
        return false;
    }
    output << report.dump(2) << '\n';
    return true;
}

}  // namespace rpf
