#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "backproj/BackProjectionEngine.hpp"
#include "sar/SarTapeToRpf.hpp"
#include "sartape2/SarTapeRecordWriter.hpp"

namespace {

std::filesystem::path makeTempPrefix(const std::string& stem) {
    const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() /
           (stem + "_" + std::to_string(now));
}

}  // namespace

TEST(BackProjectionEngineTests, WritesOutputWhenConfigured) {
    backproj::BackProjOperatorConfig op{};
    op.allowSyntheticInput = true;
    op.nPixX = 4;
    op.nPixY = 3;
    const auto prefix = makeTempPrefix("backproj");
    op.rpfBaseFileName = prefix.string();
    op.outputDebugRpf = true;

    backproj::BackProjSecondaryConfig secondary{};
    secondary.rngFilterParams.windowCoef = 0.54;
    secondary.azmFilterParams.windowCoef = 0.54;

    backproj::BackProjectionEngine engine(op, secondary);
    engine.run();

    const auto path = prefix.string() + "_backproj.tif";
    EXPECT_TRUE(std::filesystem::exists(path));
    EXPECT_TRUE(std::filesystem::exists(prefix.string() + "_backproj_meta.json"));
    EXPECT_TRUE(std::filesystem::exists(prefix.string() + "_backproj.rpf"));
    EXPECT_TRUE(std::filesystem::exists(prefix.string() + "_registration.json"));
    EXPECT_TRUE(std::filesystem::exists(prefix.string() + "_autofocus.json"));
    nlohmann::json metadata;
    std::ifstream(prefix.string() + "_backproj_meta.json") >> metadata;
    EXPECT_EQ(metadata.at("processingAlgorithm"), "legacy_magnitude_2d_fft_demo");
    EXPECT_EQ(metadata.at("syntheticInput"), true);
}

TEST(BackProjectionEngineTests, SkipsWorkWhenPixelDimsZero) {
    const auto tempDir = std::filesystem::temp_directory_path() / "backproj_empty";
    std::filesystem::create_directories(tempDir);
    const auto prev = std::filesystem::current_path();
    std::filesystem::current_path(tempDir);

    backproj::BackProjOperatorConfig op{};
    op.allowSyntheticInput = true;
    op.nPixX = 0;
    op.nPixY = 4;
    backproj::BackProjSecondaryConfig secondary{};

    backproj::BackProjectionEngine engine(op, secondary);
    engine.run();

    EXPECT_FALSE(std::filesystem::exists("backproj_output.tif"));
    std::filesystem::current_path(prev);
}

TEST(BackProjectionEngineTests, SkipsRegistrationAndAutofocusWhenDisabled) {
    backproj::BackProjOperatorConfig op{};
    op.allowSyntheticInput = true;
    op.nPixX = 2;
    op.nPixY = 2;
    op.applyAutoFocus = false;
    op.applyFrameRegistration = false;
    const auto prefix = makeTempPrefix("backproj_disabled");
    op.rpfBaseFileName = prefix.string();

    backproj::BackProjSecondaryConfig secondary{};

    backproj::BackProjectionEngine engine(op, secondary);
    engine.run();

    EXPECT_TRUE(std::filesystem::exists(prefix.string() + "_backproj.tif"));
    EXPECT_FALSE(std::filesystem::exists(prefix.string() + "_registration.json"));
    EXPECT_FALSE(std::filesystem::exists(prefix.string() + "_autofocus.json"));
}

TEST(BackProjectionEngineTests, UsesRpfInputWhenProvided) {
    const auto tempDir = std::filesystem::temp_directory_path() / "backproj_input";
    std::filesystem::create_directories(tempDir);
    const auto sarPath = tempDir / "synthetic_sartape.dat";
    const auto rpfPath = tempDir / "synthetic_sartape.rpf";

    sartape2::SarTapeRecordWriter writer;
    ASSERT_TRUE(writer.open(sarPath.string()));
    for (int record = 0; record < 3; ++record) {
        std::vector<std::int16_t> iq(128 * 2);
        for (std::size_t i = 0; i < iq.size(); ++i) {
            iq[i] = static_cast<std::int16_t>((record + 1) * 5 + static_cast<int>(i % 64));
        }
        writer.writeDataRecord(1, static_cast<std::uint16_t>(record + 1),
                               100 + static_cast<std::uint32_t>(record),
                               iq);
    }
    writer.close();

    std::string error;
    ASSERT_TRUE(sar::writeRpfFromSarTape(sarPath.string(), rpfPath.string(), 3, error)) << error;

    backproj::BackProjOperatorConfig op{};
    op.allowSyntheticInput = true;
    op.inputFilePath = tempDir.string();
    op.inputFileName = rpfPath.filename().string();
    op.nPixX = 0;
    op.nPixY = 0;

    backproj::BackProjSecondaryConfig secondary{};
    secondary.rngFilterParams.windowCoef = 0.54;
    secondary.azmFilterParams.windowCoef = 0.54;

    backproj::BackProjectionEngine engine(op, secondary);
    const auto image = engine.generateImage();

    EXPECT_GT(image.rows(), 0);
    EXPECT_GT(image.cols(), 0);
}

TEST(BackProjectionEngineTests, GenerateImageReturnsConfiguredSize) {
    backproj::BackProjOperatorConfig op{};
    op.allowSyntheticInput = true;
    op.nPixX = 5;
    op.nPixY = 4;

    backproj::BackProjSecondaryConfig secondary{};
    secondary.rngFilterParams.windowCoef = 0.54;
    secondary.azmFilterParams.windowCoef = 0.54;

    backproj::BackProjectionEngine engine(op, secondary);
    const auto image = engine.generateImage();

    EXPECT_EQ(image.rows(), 4);
    EXPECT_EQ(image.cols(), 5);
    EXPECT_GT(image.sum(), 0.0f);
}

TEST(BackProjectionEngineTests, RequiresExplicitDemoOptIn) {
    backproj::BackProjOperatorConfig op{};
    op.nPixX = 4;
    op.nPixY = 4;
    backproj::BackProjectionEngine engine(op, {});
    EXPECT_EQ(engine.runWithOutputs().size(), 0);
    EXPECT_FALSE(engine.lastError().empty());
}

TEST(BackProjectionEngineTests, NeverSubstitutesForMissingOrInvalidNamedInput) {
    for (const bool demo : {false, true}) {
        backproj::BackProjOperatorConfig op{};
        op.allowSyntheticInput = demo;
        op.nPixX = 4;
        op.nPixY = 4;
        const auto path = makeTempPrefix("backproj_invalid");
        op.inputFileName = path.string();
        backproj::BackProjectionEngine missing(op, {});
        EXPECT_EQ(missing.generateImage().size(), 0);
        std::ofstream(path) << "invalid rpf";
        backproj::BackProjectionEngine invalid(op, {});
        EXPECT_EQ(invalid.generateImage().size(), 0);
    }
}

TEST(BackProjectionEngineTests, PropagatesEveryRequestedOutputFailure) {
    for (const auto* suffix :
         {"_backproj.tif", "_backproj_norm.tif", "_backproj.raw", "_backproj_meta.json",
          "_backproj.rpf", "_registration.json", "_autofocus.json"}) {
        SCOPED_TRACE(suffix);
        backproj::BackProjOperatorConfig op{};
        op.allowSyntheticInput = true;
        op.nPixX = 4;
        op.nPixY = 4;
        op.outputDebugRpf = true;
        op.rpfBaseFileName = makeTempPrefix("backproj_write_failure").string();
        std::filesystem::create_directory(op.rpfBaseFileName + suffix);
        backproj::BackProjectionEngine engine(op, {});
        EXPECT_EQ(engine.runWithOutputs().size(), 0);
        EXPECT_FALSE(engine.lastError().empty());
    }
}

TEST(BackProjectionEngineTests, FastModeWritesRequiredOutputsOnly) {
    backproj::BackProjOperatorConfig op{};
    op.allowSyntheticInput = true;
    op.nPixX = 4;
    op.nPixY = 4;
    op.fastMode = true;
    op.rpfBaseFileName = makeTempPrefix("backproj_fast").string();
    backproj::BackProjectionEngine engine(op, {});
    EXPECT_EQ(engine.runWithOutputs().size(), 16);
    EXPECT_TRUE(engine.lastError().empty());
    EXPECT_TRUE(std::filesystem::exists(op.rpfBaseFileName + "_backproj.raw"));
    EXPECT_FALSE(std::filesystem::exists(op.rpfBaseFileName + "_backproj_norm.tif"));
}
