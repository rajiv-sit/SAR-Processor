#include "sar/SarTapeIngestPipeline.hpp"

#include <algorithm>
#include <complex>
#include <fstream>
#include <iostream>
#include <string>

#include <nlohmann/json.hpp>

#include "sar/SarTapeReader.hpp"
#include "sar/SarTapeConstants.hpp"
#include "sar/SarSceneParams.hpp"

namespace sar {

namespace {

constexpr double kSpeedOfLight = 299792458.0;

SarSceneParams buildSceneParams(const SarSceneHeader& header,
                                std::uint32_t numLines,
                                bool outputComplexIq) {
    SarSceneParams params{};
    params.carrierFreq = header.radarFreq * 1e6;
    params.lambda = (params.carrierFreq > 0.0) ? (kSpeedOfLight / params.carrierFreq) : 0.0;
    params.pulseLen = header.radarPulseWidth * 1e-9;
    params.sampRate = header.samplingFreq * 1e6;
    params.linFMRate = header.linearFMRate * 1e12;
    params.velocity = header.velocityT0;
    params.rangeSc = header.targRangeSceCtr;
    params.etaSc = header.targEtaSceCtr * (3.14159265358979323846 / 180.0);
    params.prf = (header.pri > 0) ? (1.0 / (header.pri * 1e-6)) : 0.0;
    params.numLines = numLines;
    params.numRngSamp = static_cast<std::uint32_t>(SarTapeConstants::kRecordSize / 2);
    params.dataType = outputComplexIq ? "c" : "s";
    return params;
}

nlohmann::json toJson(const SarSceneHeader& header) {
    return {
        {"recordLength", header.recordLength},
        {"videoByteCount", header.videoByteCount},
        {"byteCount", header.byteCount},
        {"sceneNumber", header.sceneNumber},
        {"timeStampStartCollect", header.timeStampStartCollect},
        {"initRangeDelay", header.initRangeDelay},
        {"rangeDelayIncr", header.rangeDelayIncr},
        {"numPulsesSpot", header.numPulsesSpot},
        {"endCountStrip", header.endCountStrip},
        {"sarTapeVolNum", header.sarTapeVolNum},
        {"missionId", header.missionId},
        {"samplingFreq", header.samplingFreq},
        {"sarMode", header.sarMode},
        {"unUsed1", header.unUsed1},
        {"timeStampT0", header.timeStampT0},
        {"pri", header.pri},
        {"targSelectMethod", header.targSelectMethod},
        {"recvrGain", header.recvrGain},
        {"headingT0", header.headingT0},
        {"velocityT0", header.velocityT0},
        {"trackAngleT0", header.trackAngleT0},
        {"altitudeT0", header.altitudeT0},
        {"targLatitudeT0", header.targLatitudeT0},
        {"targLongitudeT0", header.targLongitudeT0},
        {"targRangeT0", header.targRangeT0},
        {"targAzimuthT0", header.targAzimuthT0},
        {"targDepAngleT0", header.targDepAngleT0},
        {"targRangeSceCtr", header.targRangeSceCtr},
        {"targEtaSceCtr", header.targEtaSceCtr},
        {"targDepAngSceCtr", header.targDepAngSceCtr},
        {"tauAmplitude", header.tauAmplitude},
        {"radarFreq", header.radarFreq},
        {"radarPulseWidth", header.radarPulseWidth},
        {"linearFMRate", header.linearFMRate},
        {"priChangeFlag", header.priChangeFlag},
        {"varRngDelayIncrFlag", header.varRngDelayIncrFlag},
        {"phaseCorrFlag", header.phaseCorrFlag},
        {"rngCurvDisabledFlag", header.rngCurvDisabledFlag},
        {"ctrlCompSwVersion", header.ctrlCompSwVersion},
        {"navCompSwVersion", header.navCompSwVersion},
        {"unUsed2", header.unUsed2},
        {"endMsgCode", header.endMsgCode}
    };
}

nlohmann::json toJson(const SarSceneParams& params) {
    return {
        {"carrierFreq", params.carrierFreq},
        {"lambda", params.lambda},
        {"pulseLen", params.pulseLen},
        {"sampRate", params.sampRate},
        {"linFMRate", params.linFMRate},
        {"velocity", params.velocity},
        {"rangeSc", params.rangeSc},
        {"etaSc", params.etaSc},
        {"prf", params.prf},
        {"numLines", params.numLines},
        {"numRngSamp", params.numRngSamp},
        {"dataType", params.dataType}
    };
}

void writeComplexIq(std::ofstream& datOut, const std::vector<std::int8_t>& iqBytes) {
    const std::size_t maxIndex = iqBytes.size() - (iqBytes.size() % 2);
    for (std::size_t i = 0; i < maxIndex; i += 2) {
        const std::complex<float> sample(static_cast<float>(iqBytes[i]),
                                         static_cast<float>(iqBytes[i + 1]));
        datOut.write(reinterpret_cast<const char*>(&sample),
                     static_cast<std::streamsize>(sizeof(sample)));
    }
}

}  // namespace

std::uint32_t computeExpectedLines(const SarSceneHeader& header) {
    if (header.sarMode & SarTapeConstants::kSarModeMaskMultiScene) {
        if (header.sarMode & SarTapeConstants::kSarModeMaskRdp) {
            return SarTapeConstants::kMaxVideoRecsPerScene;
        }
        return static_cast<std::uint32_t>(header.endCountStrip);
    }
    return std::min<std::uint32_t>(SarTapeConstants::kMaxVideoRecsSpot,
                                   header.numPulsesSpot);
}

SarTapeIngestPipeline::SarTapeIngestPipeline(std::string inputPath,
                                             std::string outputPrefix,
                                             IngestOptions options)
    : inputPath_(std::move(inputPath)),
      outputPrefix_(std::move(outputPrefix)),
      options_(options) {}

std::uint32_t SarTapeIngestPipeline::run() {
    SarTapeReader reader(inputPath_);
    if (!reader.isOpen()) {
        std::cerr << "Failed to open SarTape input: " << inputPath_ << '\n';
        return 0;
    }

    const std::string datPath = outputPrefix_ + ".dat";
    const std::string vtsPath = outputPrefix_ + ".vts";
    const std::string hdrPath = outputPrefix_ + ".hdr";
    const std::string sspPath = outputPrefix_ + ".ssp";

    std::ofstream datOut(datPath, std::ios::binary);
    std::ofstream vtsOut(vtsPath);
    std::ofstream hdrOut(hdrPath);
    std::ofstream sspOut(sspPath);

    if (!datOut || !vtsOut || !hdrOut || !sspOut) {
        std::cerr << "Failed to open output files for prefix: " << outputPrefix_ << '\n';
        return 0;
    }

    SarTraceRecord record;
    std::uint32_t linesWritten = 0;
    std::uint32_t expectedLines = 0;
    bool expectedLinesKnown = false;

    while (reader.readRecord(record)) {
        if (!record.header.syncValid) {
            std::cerr << "Invalid sync word at record " << record.header.recordNumber << '\n';
            if (options_.errorPolicy == ErrorPolicy::kFatal) {
                break;
            }
        }

        if (record.header.recordType == SarTapeConstants::kRecordTypeSceneHeader &&
            reader.hasSceneHeader()) {
            const auto& sceneHeader = reader.sceneHeader();
            expectedLines = computeExpectedLines(sceneHeader);
            expectedLinesKnown = true;
        }

        if (options_.outputComplexIq) {
            writeComplexIq(datOut, record.iqBytes);
        } else {
            datOut.write(reinterpret_cast<const char*>(record.iqBytes.data()),
                         static_cast<std::streamsize>(record.iqBytes.size()));
        }
        vtsOut << record.header.timeStamp << '\n';
        ++linesWritten;

        if (expectedLinesKnown && linesWritten >= expectedLines) {
            break;
        }
    }

    if (reader.hasSceneHeader()) {
        const auto& sceneHeader = reader.sceneHeader();
        const SarSceneParams sceneParams = buildSceneParams(sceneHeader,
                                                            linesWritten,
                                                            options_.outputComplexIq);

        hdrOut << toJson(sceneHeader).dump(2) << '\n';
        sspOut << toJson(sceneParams).dump(2) << '\n';
    }

    return linesWritten;
}

}  // namespace sar
