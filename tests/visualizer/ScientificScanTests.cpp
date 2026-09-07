#include <gtest/gtest.h>

#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <vector>

#include "visualizer/ScientificScan.hpp"

namespace {

using sar::visualizer::loadScientificScan;
using sar::visualizer::ScientificStageStatus;

class ScientificScanTests : public ::testing::Test {
   protected:
    std::filesystem::path directory;
    std::filesystem::path manifest;
    nlohmann::json metadata;
    bool directoryOwned = false;

    void SetUp() override {
        static std::atomic<unsigned> serial{0};
        const auto timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
        directory = std::filesystem::temp_directory_path() /
                    ("sar_scan_" + std::to_string(timestamp) + "_" + std::to_string(++serial));
        directoryOwned = std::filesystem::create_directory(directory);
        ASSERT_TRUE(directoryOwned);
        manifest = directory / "scan.sarscan";
        metadata = {{"schema", "sar-scientific-scan-v1"},
                    {"label", "Sandia acquisition"},
                    {"source", "measured.ntf"},
                    {"default_stage", "geocoded"},
                    {"stages",
                     {{{"id", "range"},
                       {"label", "Range compression"},
                       {"status", "unavailable"},
                       {"reason", "Focused input; upstream range compression"}},
                      {{"id", "geocoded"},
                       {"label", "Geocoded magnitude"},
                       {"status", "available"},
                       {"manifest", "geocoded.sarframe"}}}}};
        writeFrame();
        writeManifest();
    }

    void TearDown() override {
        std::error_code ignored;
        if (directoryOwned) {
            std::filesystem::remove_all(directory, ignored);
        }
    }

    void writeManifest() const { std::ofstream(manifest) << metadata; }

    void writeFrame() const {
        nlohmann::json frame = {{"schema", "sar-scientific-frame-v1"},
                                {"width", 3},
                                {"height", 2},
                                {"product", "geocoded_magnitude"},
                                {"units", "uncalibrated magnitude DN"},
                                {"source", "measured.ntf"},
                                {"data_file", "geocoded.raw"},
                                {"geolocation",
                                 {{"rows", {0, 1}},
                                  {"cols", {0, 2}},
                                  {"longitude", {{-107, -105}, {-107, -105}}},
                                  {"latitude", {{35, 35}, {34, 34}}},
                                  {"interpolation_error_m", 0.002}}}};
        std::ofstream(directory / "geocoded.sarframe") << frame;
        std::ofstream output(directory / "geocoded.raw", std::ios::binary);
        const auto word = [&](std::uint32_t value) {
            const std::array<unsigned char, 4> bytes = {
                static_cast<unsigned char>(value & 255),
                static_cast<unsigned char>((value >> 8) & 255),
                static_cast<unsigned char>((value >> 16) & 255),
                static_cast<unsigned char>((value >> 24) & 255)};
            output.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        };
        word(3);
        word(2);
        for (float value : {0.0F, 1.0F, 2.0F, 3.0F, 4.0F, 5.0F}) {
            word(std::bit_cast<std::uint32_t>(value));
        }
    }

    void expectInvalid() {
        writeManifest();
        EXPECT_THROW(loadScientificScan(manifest), std::runtime_error);
    }
};

TEST_F(ScientificScanTests, LoadsAvailableFrameAndPreservesUpstreamUnavailableStageReason) {
    const auto scan = loadScientificScan(manifest);
    EXPECT_EQ(scan.label, "Sandia acquisition");
    EXPECT_EQ(scan.source, "measured.ntf");
    EXPECT_EQ(scan.defaultStage, "geocoded");
    EXPECT_EQ(scan.defaultStageIndex, 1);
    EXPECT_EQ(scan.totalPixels, 6);
    ASSERT_EQ(scan.stages.size(), 2);
    EXPECT_EQ(scan.stages[0].status, ScientificStageStatus::Unavailable);
    EXPECT_FALSE(scan.stages[0].frame);
    EXPECT_TRUE(scan.stages[0].manifestPath.empty());
    EXPECT_EQ(scan.stages[0].reason, "Focused input; upstream range compression");
    const auto& displayed = scan.stages[scan.defaultStageIndex];
    EXPECT_EQ(displayed.status, ScientificStageStatus::Available);
    EXPECT_EQ(displayed.id, "geocoded");
    EXPECT_EQ(displayed.label, "Geocoded magnitude");
    EXPECT_TRUE(displayed.reason.empty());
    EXPECT_EQ(displayed.manifestPath, directory / "geocoded.sarframe");
    ASSERT_TRUE(displayed.frame);
    EXPECT_EQ(displayed.frame->pixels, (std::vector<float>{0, 1, 2, 3, 4, 5}));
    ASSERT_TRUE(displayed.frame->geolocation);
    EXPECT_EQ(displayed.frame->product, "geocoded_magnitude");
}

TEST_F(ScientificScanTests, EnforcesCombinedPixelBudgetBeforeAllocatingFollowingFrame) {
    auto additional = metadata["stages"][1];
    additional["id"] = "second";
    metadata["stages"].push_back(additional);
    writeManifest();
    EXPECT_EQ(loadScientificScan(manifest, 12).totalPixels, 12);
    EXPECT_THROW(loadScientificScan(manifest, 11), std::runtime_error);
    EXPECT_THROW(loadScientificScan(manifest, 6), std::runtime_error);
    EXPECT_THROW(loadScientificScan(manifest, 0), std::runtime_error);
    EXPECT_THROW(loadScientificScan(manifest, 64'000'001), std::runtime_error);
}

TEST_F(ScientificScanTests, AllowsSixteenStagesButRejectsSeventeen) {
    metadata["stages"] = nlohmann::json::array();
    for (int i = 0; i < 16; ++i) {
        metadata["stages"].push_back({{"id", "stage" + std::to_string(i)},
                                      {"label", "Magnitude"},
                                      {"status", "available"},
                                      {"manifest", "geocoded.sarframe"}});
    }
    metadata["default_stage"] = "stage15";
    writeManifest();
    EXPECT_EQ(loadScientificScan(manifest).stages.size(), 16);
    auto additional = metadata["stages"][0];
    additional["id"] = "stage16";
    metadata["stages"].push_back(additional);
    expectInvalid();
}

TEST_F(ScientificScanTests, RequiresVersionAndAllTopLevelMetadata) {
    const auto original = metadata;
    for (const auto* key : {"schema", "label", "source", "default_stage", "stages"}) {
        metadata = original;
        metadata.erase(key);
        expectInvalid();
    }
    metadata = original;
    metadata["schema"] = "wrong-version";
    expectInvalid();
    metadata["schema"] = "sar-scientific-scan-v1";
    metadata["stages"] = nlohmann::json::array();
    expectInvalid();
    metadata["stages"] = "not a list";
    expectInvalid();
}

TEST_F(ScientificScanTests, RequiresStageMetadataUniqueIdsAndSupportedStatus) {
    const auto original = metadata;
    for (const auto* key : {"id", "label", "status", "manifest"}) {
        metadata = original;
        metadata["stages"][1].erase(key);
        expectInvalid();
    }
    metadata = original;
    metadata["stages"][0]["id"] = "geocoded";
    // Keep the first stage non-default so duplicate detection is exercised.
    metadata["default_stage"] = "absent";
    expectInvalid();
    metadata = original;
    metadata["stages"][0]["status"] = "loading";
    expectInvalid();
}

TEST_F(ScientificScanTests, UnavailableStageRequiresReasonAndCannotReferenceAFrame) {
    const auto original = metadata;
    metadata["stages"][0].erase("reason");
    expectInvalid();
    metadata = original;
    metadata["stages"][0]["reason"] = "";
    expectInvalid();
    metadata = original;
    metadata["stages"][0]["manifest"] = "geocoded.sarframe";
    expectInvalid();
    metadata["stages"][0]["manifest"] = nullptr;
    expectInvalid();
}

TEST_F(ScientificScanTests, DefaultStageMustExistAndBeAvailable) {
    metadata["default_stage"] = "range";
    expectInvalid();
    metadata["default_stage"] = "not-here";
    expectInvalid();
}

TEST_F(ScientificScanTests, RejectsEmptyOversizedNulAndNonStringText) {
    for (const nlohmann::json& invalid :
         std::vector<nlohmann::json>{"", 2, std::string(4097, 'x'), std::string("a\0b", 3)}) {
        metadata["label"] = invalid;
        expectInvalid();
    }
}

TEST_F(ScientificScanTests, RejectsAbsoluteNestedTraversalAndNonFileStagePaths) {
    for (const auto* name :
         {"..", ".", "../geocoded.sarframe", "sub/geocoded.sarframe", "sub\\geocoded.sarframe",
          "C:geocoded.sarframe", "/geocoded.sarframe", "missing.sarframe"}) {
        metadata["stages"][1]["manifest"] = name;
        expectInvalid();
    }
    std::filesystem::create_directory(directory / "folder");
    metadata["stages"][1]["manifest"] = "folder";
    expectInvalid();
}

TEST_F(ScientificScanTests, RejectsSymlinkEscapeWhenPlatformAllowsSymlinks) {
    const auto outside = directory / "outside";
    const auto link = directory / "redirect.sarframe";
    std::filesystem::create_directory(outside);
    std::ofstream(outside / "real.sarframe") << "{}";
    std::error_code error;
    std::filesystem::create_symlink(outside / "real.sarframe", link, error);
    if (error) {
        GTEST_SKIP() << "Creating symlinks is unavailable: " << error.message();
    }
    metadata["stages"][1]["manifest"] = "redirect.sarframe";
    expectInvalid();
}

TEST_F(ScientificScanTests, RejectsInvalidFramePayloadWithBothFileNamesInError) {
    std::ofstream(directory / "geocoded.raw", std::ios::binary) << "bad";
    try {
        loadScientificScan(manifest);
        FAIL() << "Expected corrupt frame failure";
    } catch (const std::runtime_error& error) {
        EXPECT_NE(std::string(error.what()).find("scan.sarscan"), std::string::npos);
        EXPECT_NE(std::string(error.what()).find("geocoded.sarframe"), std::string::npos);
    }
}

TEST_F(ScientificScanTests, MissingMalformedAndOversizedManifestAreRejected) {
    EXPECT_THROW(loadScientificScan(directory / "missing.sarscan"), std::runtime_error);
    std::ofstream(manifest) << "{ invalid json";
    EXPECT_THROW(loadScientificScan(manifest), std::runtime_error);
    std::ofstream(manifest) << std::string(2'000'001, ' ');
    EXPECT_THROW(loadScientificScan(manifest), std::runtime_error);
}

}  // namespace
