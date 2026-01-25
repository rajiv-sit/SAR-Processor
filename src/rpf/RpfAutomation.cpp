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

nlohmann::json toJson(const ImageRect& rect) {
    return {
        {"startLine", rect.startLine},
        {"startPixel", rect.startPixel},
        {"numLines", rect.numLines},
        {"numPixels", rect.numPixels}
    };
}

nlohmann::json toJson(const AcquisitionMetadata& meta) {
    return {
        {"frameSeqNum", meta.frameSeqNum},
        {"pixelType", meta.pixelType},
        {"rspInhibit", meta.rspInhibit},
        {"pixelMarginStart", meta.pixelMarginStart},
        {"pixelMarginEnd", meta.pixelMarginEnd},
        {"lineMarginStart", meta.lineMarginStart},
        {"lineMarginEnd", meta.lineMarginEnd}
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

nlohmann::json toJson(const LatLongGrid& grid) {
    return {
        {"lineNumber", grid.lineNumber},
        {"beginGrSrRatio", grid.beginGrSrRatio},
        {"midGrSrRatio", grid.midGrSrRatio},
        {"endGrSrRatio", grid.endGrSrRatio},
        {"beginLatitude", grid.beginLatitude},
        {"beginLongitude", grid.beginLongitude},
        {"midLatitude", grid.midLatitude},
        {"midLongitude", grid.midLongitude},
        {"endLatitude", grid.endLatitude},
        {"endLongitude", grid.endLongitude}
    };
}

bool writeTextLine(std::ofstream& output, const std::string& key, const std::string& value) {
    output << key << "=" << value << '\n';
    return static_cast<bool>(output);
}

bool writeTextLine(std::ofstream& output, const std::string& key, std::int64_t value) {
    output << key << "=" << value << '\n';
    return static_cast<bool>(output);
}

bool writeTextLine(std::ofstream& output, const std::string& key, double value) {
    output << key << "=" << value << '\n';
    return static_cast<bool>(output);
}

bool writeTextStats(std::ofstream& output, const std::string& prefix, const pta::PtaStats& stats) {
    writeTextLine(output, prefix + ".irw", stats.irw);
    writeTextLine(output, prefix + ".mslr", stats.mslr);
    writeTextLine(output, prefix + ".islr", stats.islr);
    writeTextLine(output, prefix + ".pos", stats.pos);
    writeTextLine(output, prefix + ".maxPower", stats.maxPower);
    return static_cast<bool>(output);
}

}  // namespace

bool RpfAutomation::writeAnnotationReport(const std::string& path,
                                          const AnnotationStruct& annotation,
                                          const LatLongGrid& grid,
                                          const pta::PtaStats& xStats,
                                          const pta::PtaStats& yStats) const {
    const bool asJson = (path.size() >= 5 && path.substr(path.size() - 5) == ".json");
    std::ofstream output(path);
    if (!output) {
        return false;
    }

    if (asJson) {
        nlohmann::json report{
            {"fileName", annotation.fileName},
            {"radarMode", annotation.fileIdParams.radarMode},
            {"fileType", annotation.fileIdParams.fileType},
            {"geolocationGridNumLines", annotation.latLongOutput.geolocationGridNumLines},
            {"imageDataChunkHeader", toJson(annotation.imageDataChunkHeader)},
            {"imageRect", toJson(annotation.imageRect)},
            {"acquisition", toJson(annotation.acquisition)},
            {"notes", {{"summary", annotation.notes.summary}}},
            {"latLongGridCount", grid.lineNumber.size()},
            {"latLongGrid", toJson(grid)},
            {"ptaStats", {{"x", toJson(xStats)}, {"y", toJson(yStats)}}}
        };
        output << report.dump(2) << '\n';
    } else {
        writeTextLine(output, "fileName", annotation.fileName);
        writeTextLine(output, "radarMode", annotation.fileIdParams.radarMode);
        writeTextLine(output, "fileType", annotation.fileIdParams.fileType);
        writeTextLine(output, "gridLines", annotation.latLongOutput.geolocationGridNumLines);
        writeTextLine(output, "imageRect.startLine", annotation.imageRect.startLine);
        writeTextLine(output, "imageRect.startPixel", annotation.imageRect.startPixel);
        writeTextLine(output, "imageRect.numLines", annotation.imageRect.numLines);
        writeTextLine(output, "imageRect.numPixels", annotation.imageRect.numPixels);
        writeTextStats(output, "pta.x", xStats);
        writeTextStats(output, "pta.y", yStats);
    }

    return static_cast<bool>(output);
}

bool RpfAutomation::writeAutoPtaReport(const std::string& path,
                                       const AnnotationStruct& annotation,
                                       const std::vector<AutoPtaEntry>& entries) const {
    const bool asJson = (path.size() >= 5 && path.substr(path.size() - 5) == ".json");
    std::ofstream output(path);
    if (!output) {
        return false;
    }

    if (asJson) {
        nlohmann::json report{
            {"fileName", annotation.fileName},
            {"radarMode", annotation.fileIdParams.radarMode},
            {"fileType", annotation.fileIdParams.fileType},
            {"entries", nlohmann::json::array()}
        };
        for (const auto& entry : entries) {
            report["entries"].push_back({
                {"line", entry.line},
                {"x", toJson(entry.xStats)},
                {"y", toJson(entry.yStats)}
            });
        }
        output << report.dump(2) << '\n';
    } else {
        writeTextLine(output, "fileName", annotation.fileName);
        writeTextLine(output, "radarMode", annotation.fileIdParams.radarMode);
        writeTextLine(output, "fileType", annotation.fileIdParams.fileType);
        writeTextLine(output, "entryCount", static_cast<std::int64_t>(entries.size()));
        for (const auto& entry : entries) {
            output << "line=" << entry.line << '\n';
            writeTextStats(output, "pta.x", entry.xStats);
            writeTextStats(output, "pta.y", entry.yStats);
        }
    }
    return static_cast<bool>(output);
}

bool RpfAutomation::writeStripmapLinesToReprocess(const std::string& path,
                                                  const std::vector<int>& lines) const {
    const bool asJson = (path.size() >= 5 && path.substr(path.size() - 5) == ".json");
    std::ofstream output(path);
    if (!output) {
        return false;
    }
    if (asJson) {
        nlohmann::json report{{"lines", lines}, {"count", lines.size()}};
        output << report.dump(2) << '\n';
    } else {
        writeTextLine(output, "count", static_cast<std::int64_t>(lines.size()));
        for (int line : lines) {
            output << line << '\n';
        }
    }
    return static_cast<bool>(output);
}

}  // namespace rpf
