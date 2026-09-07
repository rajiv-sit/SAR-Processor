#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

#include <gtest/gtest.h>

#include "backproj/BackProjConfigLoader.hpp"

namespace {

std::filesystem::path makeTempPath(const std::string& stem) {
    const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() /
           (stem + "_" + std::to_string(now) + ".json");
}

}  // namespace

TEST(BackProjConfigLoaderTests, MissingFilesReturnDefaults) {
    backproj::BackProjConfigLoader loader;
    const auto opConfig = loader.loadOperatorConfig("missing_operator.json");
    const auto secConfig = loader.loadSecondaryConfig("missing_secondary.json");

    EXPECT_EQ(opConfig.inputNumFiles, 0u);
    EXPECT_FALSE(opConfig.allowSyntheticInput);
    EXPECT_EQ(secConfig.blockSizeInBytes, 0u);
}

TEST(BackProjConfigLoaderTests, LoadsOperatorConfigFields) {
    const auto path = makeTempPath("operator");
    std::ofstream output(path);
    ASSERT_TRUE(output);
    output << R"json({
        "outputDebugRpf": true, "allowSyntheticInput": true,
        "rpfBaseFileName": "rpf_base",
        "inputFilePath": "data/raw",
        "inputFileName": "input.dat",
        "inputNumFiles": 2,
        "firstRangeLine": 5,
        "numLinesToProcess": 100,
        "nPixX": 128,
        "nPixY": 64,
        "imageOffset": [1.0, 2.0],
        "collapseFactor": 3,
        "apOverlapFrac": 0.25,
        "maxNumFrames": 4,
        "applyAutoFocus": false,
        "applyFrameRegistration": false,
        "numTilesX": 2,
        "numTilesY": 3,
        "useGpu": true
    })json";
    output.close();

    backproj::BackProjConfigLoader loader;
    const auto config = loader.loadOperatorConfig(path.string());
    EXPECT_TRUE(config.outputDebugRpf);
    EXPECT_TRUE(config.allowSyntheticInput);
    EXPECT_EQ(config.rpfBaseFileName, "rpf_base");
    EXPECT_EQ(config.inputFilePath, "data/raw");
    EXPECT_EQ(config.inputFileName, "input.dat");
    EXPECT_EQ(config.inputNumFiles, 2u);
    EXPECT_EQ(config.firstRangeLine, 5u);
    EXPECT_EQ(config.numLinesToProcess, 100u);
    EXPECT_EQ(config.nPixX, 128u);
    EXPECT_EQ(config.nPixY, 64u);
    ASSERT_EQ(config.imageOffset.size(), 2u);
    EXPECT_DOUBLE_EQ(config.imageOffset[0], 1.0);
    EXPECT_DOUBLE_EQ(config.imageOffset[1], 2.0);
    EXPECT_EQ(config.collapseFactor, 3u);
    EXPECT_DOUBLE_EQ(config.apOverlapFrac, 0.25);
    EXPECT_EQ(config.maxNumFrames, 4u);
    EXPECT_FALSE(config.applyAutoFocus);
    EXPECT_FALSE(config.applyFrameRegistration);
    EXPECT_EQ(config.numTilesX, 2u);
    EXPECT_EQ(config.numTilesY, 3u);
    EXPECT_TRUE(config.useGpu);
}

TEST(BackProjConfigLoaderTests, LoadsSecondaryConfigFields) {
    const auto path = makeTempPath("secondary");
    std::ofstream output(path);
    ASSERT_TRUE(output);
    output << R"json({
        "algorithmSelection": "BP",
        "rngFilterParams": { "windowCoef": 0.5, "windowBroadening": 1.5, "scalingMethod": "none" },
        "azmFilterParams": { "windowCoef": 0.7, "windowBroadening": 2.5, "scalingMethod": "peak" },
        "quadParams": { "minSubImageSize": 64, "azOverSampFact": 2.0, "nExtraRngSamps": 8 },
        "frameRegistrationParams": { "alphaAccum": 0.1, "preShiftImageGrid": true, "regisSearchSize": 9 },
        "rgCompMode": "fft",
        "blockSizeInBytes": 4096,
        "nExtraRngSamps": 16,
        "tileOverlap": 2,
        "edgeTaperPixels": 4,
        "applyIqCalCorrection": true,
        "applyAgcCorrection": true,
        "applyStcCorrection": false
    })json";
    output.close();

    backproj::BackProjConfigLoader loader;
    const auto config = loader.loadSecondaryConfig(path.string());
    EXPECT_EQ(config.algorithmSelection, "BP");
    EXPECT_DOUBLE_EQ(config.rngFilterParams.windowCoef, 0.5);
    EXPECT_DOUBLE_EQ(config.rngFilterParams.windowBroadening, 1.5);
    EXPECT_EQ(config.rngFilterParams.scalingMethod, "none");
    EXPECT_DOUBLE_EQ(config.azmFilterParams.windowCoef, 0.7);
    EXPECT_DOUBLE_EQ(config.azmFilterParams.windowBroadening, 2.5);
    EXPECT_EQ(config.azmFilterParams.scalingMethod, "peak");
    EXPECT_EQ(config.quadParams.minSubImageSize, 64u);
    EXPECT_DOUBLE_EQ(config.quadParams.azOverSampFact, 2.0);
    EXPECT_EQ(config.quadParams.nExtraRngSamps, 8u);
    EXPECT_DOUBLE_EQ(config.frameRegistrationParams.alphaAccum, 0.1);
    EXPECT_TRUE(config.frameRegistrationParams.preShiftImageGrid);
    EXPECT_EQ(config.frameRegistrationParams.regisSearchSize, 9u);
    EXPECT_EQ(config.rgCompMode, "fft");
    EXPECT_EQ(config.blockSizeInBytes, 4096u);
    EXPECT_EQ(config.nExtraRngSamps, 16u);
    EXPECT_EQ(config.tileOverlap, 2u);
    EXPECT_EQ(config.edgeTaperPixels, 4u);
    EXPECT_TRUE(config.applyIqCalCorrection);
    EXPECT_TRUE(config.applyAgcCorrection);
    EXPECT_FALSE(config.applyStcCorrection);
}
