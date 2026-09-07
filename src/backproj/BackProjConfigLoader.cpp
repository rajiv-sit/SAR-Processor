#include "backproj/BackProjConfigLoader.hpp"

#include <fstream>

#include <nlohmann/json.hpp>

namespace backproj {

namespace {

nlohmann::json getObjectOrEmpty(const nlohmann::json& root, const char* key) {
    if (!root.is_object()) {
        return nlohmann::json::object();
    }
    auto it = root.find(key);
    if (it == root.end() || it->is_null() || !it->is_object()) {
        return nlohmann::json::object();
    }
    return *it;
}

}  // namespace

BackProjOperatorConfig BackProjConfigLoader::loadOperatorConfig(const std::string& path) const {
    std::ifstream input(path);
    BackProjOperatorConfig config{};
    if (!input) {
        return config;
    }

    nlohmann::json json;
    input >> json;
    if (!json.is_object()) {
        return config;
    }

    config.outputDebugRpf = json.value("outputDebugRpf", false);
    config.allowSyntheticInput = json.value("allowSyntheticInput", false);
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
    config.fastMode = json.value("fastMode", false);

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
    if (!json.is_object()) {
        return config;
    }

    config.algorithmSelection = json.value("algorithmSelection", "");
    const auto rngFilter = getObjectOrEmpty(json, "rngFilterParams");
    config.rngFilterParams.windowCoef = rngFilter.value("windowCoef", 0.0);
    config.rngFilterParams.windowBroadening = rngFilter.value("windowBroadening", 0.0);
    config.rngFilterParams.scalingMethod = rngFilter.value("scalingMethod", "");

    const auto azmFilter = getObjectOrEmpty(json, "azmFilterParams");
    config.azmFilterParams.windowCoef = azmFilter.value("windowCoef", 0.0);
    config.azmFilterParams.windowBroadening = azmFilter.value("windowBroadening", 0.0);
    config.azmFilterParams.scalingMethod = azmFilter.value("scalingMethod", "");

    const auto quadParams = getObjectOrEmpty(json, "quadParams");
    config.quadParams.minSubImageSize = quadParams.value("minSubImageSize", 32u);
    config.quadParams.azOverSampFact = quadParams.value("azOverSampFact", 1.0);
    config.quadParams.nExtraRngSamps = quadParams.value("nExtraRngSamps", 0u);
    config.quadParams.nRngTaper = quadParams.value("nRngTaper", 0u);
    config.quadParams.nAzmTaper = quadParams.value("nAzmTaper", 0u);
    config.quadParams.nPadAzFftEachEnd = quadParams.value("nPadAzFftEachEnd", 0u);

    const auto frameReg = getObjectOrEmpty(json, "frameRegistrationParams");
    config.frameRegistrationParams.alphaAccum = frameReg.value("alphaAccum", 0.0);
    config.frameRegistrationParams.preShiftImageGrid = frameReg.value("preShiftImageGrid", false);
    config.frameRegistrationParams.regisSearchSize = frameReg.value("regisSearchSize", 0u);
    config.frameRegistrationParams.marginBlanking = frameReg.value("marginBlanking", 0u);
    config.frameRegistrationParams.chipSize = frameReg.value("chipSize", 0u);
    config.frameRegistrationParams.magFactor = frameReg.value("magFactor", 0u);
    config.frameRegistrationParams.accumPow = frameReg.value("accumPow", 0.0);
    config.frameRegistrationParams.detPow = frameReg.value("detPow", 0.0);

    const auto autofocus = getObjectOrEmpty(json, "autofocusParams");
    config.autofocusParams.selectionMethod = autofocus.value("selectionMethod", "");
    config.autofocusParams.minMetricDelta = autofocus.value("minMetricDelta", 0.0);
    config.autofocusParams.maxPoints = autofocus.value("maxPoints", 16u);
    config.autofocusParams.maxFrames = autofocus.value("maxFrames", 0u);

    const auto imageScaling = getObjectOrEmpty(json, "imageScaling");
    config.imageScaling.lowerPercentile = imageScaling.value("lowerPercentile", 0.0);
    config.imageScaling.upperPercentile = imageScaling.value("upperPercentile", 1.0);
    config.imageScaling.outputScalingFactor = imageScaling.value("outputScalingFactor", 1.0);

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
