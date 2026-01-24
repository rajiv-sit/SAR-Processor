#include "backproj/BackProjConfigLoader.hpp"

#include <fstream>

#include <nlohmann/json.hpp>

namespace backproj {

BackProjOperatorConfig BackProjConfigLoader::loadOperatorConfig(const std::string& path) const {
    std::ifstream input(path);
    BackProjOperatorConfig config{};
    if (!input) {
        return config;
    }

    nlohmann::json json;
    input >> json;

    config.outputDebugRpf = json.value("outputDebugRpf", false);
    config.rpfBaseFileName = json.value("rpfBaseFileName", "");
    config.inputFilePath = json.value("inputFilePath", "");
    config.inputFileName = json.value("inputFileName", "");
    config.inputNumFiles = json.value("inputNumFiles", 0u);
    config.replicaSelection = json.value("replicaSelection", "");
    config.pulseReplicaFileName = json.value("pulseReplicaFileName", "");
    config.firstRangeLine = json.value("firstRangeLine", 1u);
    config.numLinesToProcess = json.value("numLinesToProcess", 0u);
    config.outputProjection = json.value("outputProjection", "");
    config.nPixX = json.value("nPixX", 0u);
    config.nPixY = json.value("nPixY", 0u);
    config.imageOffsetSpec = json.value("imageOffsetSpec", "");
    config.imageOffset = json.value("imageOffset", std::vector<double>{});
    config.collapseFactor = json.value("collapseFactor", 1u);
    config.apOverlapFrac = json.value("apOverlapFrac", 0.0);
    config.maxNumFrames = json.value("maxNumFrames", 0u);
    config.applyAutoFocus = json.value("applyAutoFocus", true);
    config.applyFrameRegistration = json.value("applyFrameRegistration", true);
    config.azmResSelection = json.value("azmResSelection", "");
    config.azimuthCollapseFactor = json.value("azimuthCollapseFactor", 1.0);
    config.azimuthRes = json.value("azimuthRes", 0.0);
    config.pixSpacingSelection = json.value("pixSpacingSelection", "");
    config.pixelSpacing = json.value("pixelSpacing", 0.0);
    config.outputTiffFrames = json.value("outputTiffFrames", "");
    config.tileSelection = json.value("tileSelection", "");
    config.numTilesY = json.value("numTilesY", 1u);
    config.numTilesX = json.value("numTilesX", 1u);
    config.useGpu = json.value("useGpu", false);

    return config;
}

BackProjSecondaryConfig BackProjConfigLoader::loadSecondaryConfig(const std::string& path) const {
    std::ifstream input(path);
    BackProjSecondaryConfig config{};
    if (!input) {
        return config;
    }

    nlohmann::json json;
    input >> json;

    config.algorithmSelection = json.value("algorithmSelection", "");
    config.rngFilterParams.windowCoef = json.value("rngFilterParams", nlohmann::json{}).value("windowCoef", 0.0);
    config.rngFilterParams.windowBroadening =
        json.value("rngFilterParams", nlohmann::json{}).value("windowBroadening", 0.0);
    config.rngFilterParams.scalingMethod =
        json.value("rngFilterParams", nlohmann::json{}).value("scalingMethod", "");
    config.azmFilterParams.windowCoef = json.value("azmFilterParams", nlohmann::json{}).value("windowCoef", 0.0);
    config.azmFilterParams.windowBroadening =
        json.value("azmFilterParams", nlohmann::json{}).value("windowBroadening", 0.0);
    config.azmFilterParams.scalingMethod =
        json.value("azmFilterParams", nlohmann::json{}).value("scalingMethod", "");

    config.quadParams.minSubImageSize = json.value("quadParams", nlohmann::json{}).value("minSubImageSize", 32u);
    config.quadParams.azOverSampFact = json.value("quadParams", nlohmann::json{}).value("azOverSampFact", 1.0);
    config.quadParams.nExtraRngSamps = json.value("quadParams", nlohmann::json{}).value("nExtraRngSamps", 0u);
    config.quadParams.nRngTaper = json.value("quadParams", nlohmann::json{}).value("nRngTaper", 0u);
    config.quadParams.nAzmTaper = json.value("quadParams", nlohmann::json{}).value("nAzmTaper", 0u);
    config.quadParams.nPadAzFftEachEnd =
        json.value("quadParams", nlohmann::json{}).value("nPadAzFftEachEnd", 0u);

    config.frameRegistrationParams.alphaAccum =
        json.value("frameRegistrationParams", nlohmann::json{}).value("alphaAccum", 0.0);
    config.frameRegistrationParams.preShiftImageGrid =
        json.value("frameRegistrationParams", nlohmann::json{}).value("preShiftImageGrid", false);
    config.frameRegistrationParams.regisSearchSize =
        json.value("frameRegistrationParams", nlohmann::json{}).value("regisSearchSize", 0u);
    config.frameRegistrationParams.marginBlanking =
        json.value("frameRegistrationParams", nlohmann::json{}).value("marginBlanking", 0u);
    config.frameRegistrationParams.chipSize =
        json.value("frameRegistrationParams", nlohmann::json{}).value("chipSize", 0u);
    config.frameRegistrationParams.magFactor =
        json.value("frameRegistrationParams", nlohmann::json{}).value("magFactor", 0u);
    config.frameRegistrationParams.accumPow =
        json.value("frameRegistrationParams", nlohmann::json{}).value("accumPow", 0.0);
    config.frameRegistrationParams.detPow =
        json.value("frameRegistrationParams", nlohmann::json{}).value("detPow", 0.0);

    config.rgCompMode = json.value("rgCompMode", "");
    config.blockSizeInBytes = json.value("blockSizeInBytes", 0u);
    config.nExtraRngSamps = json.value("nExtraRngSamps", 0u);
    config.tileOverlap = json.value("tileOverlap", 0u);
    config.edgeTaperPixels = json.value("edgeTaperPixels", 0u);
    config.applyIqCalCorrection = json.value("applyIqCalCorrection", false);
    config.applyAgcCorrection = json.value("applyAgcCorrection", false);
    config.applyStcCorrection = json.value("applyStcCorrection", false);

    return config;
}

}  // namespace backproj
