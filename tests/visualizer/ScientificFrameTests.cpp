#include <gtest/gtest.h>

#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <nlohmann/json.hpp>
#include <numbers>
#include <string>
#include <vector>

#include "visualizer/ScientificFrame.hpp"

namespace {

using sar::visualizer::displayToGray;
using sar::visualizer::geolocate;
using sar::visualizer::loadScientificFrame;
using sar::visualizer::magnitudeToGray;
using sar::visualizer::ScientificFrame;

class ScientificFrameTests : public ::testing::Test {
   protected:
    std::filesystem::path directory;
    std::filesystem::path manifest;
    nlohmann::json metadata;
    bool directoryOwned = false;

    void SetUp() override {
        static std::atomic<unsigned> serial{0};
        const auto timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
        directory =
            std::filesystem::temp_directory_path() /
            ("sar_scientific_" + std::to_string(timestamp) + "_" + std::to_string(++serial));
        directoryOwned = std::filesystem::create_directory(directory);
        ASSERT_TRUE(directoryOwned) << "Test fixture directory must be newly created";
        manifest = directory / "image.sarframe";
        metadata = {{"schema", "sar-scientific-frame-v1"},
                    {"width", 3},
                    {"height", 2},
                    {"product", "scene_local_magnitude"},
                    {"units", "uncalibrated magnitude DN"},
                    {"source", "source/geocoded.tif"},
                    {"data_file", "image.raw"}};
        writePixels({0, 1, 10, 100, 1000, std::numeric_limits<float>::quiet_NaN()});
        writeManifest();
    }

    void TearDown() override {
        std::error_code ignored;
        // Only this fixture's uniquely named directory and generated files.
        if (directoryOwned) {
            std::filesystem::remove_all(directory, ignored);
        }
    }

    void writeManifest() const { std::ofstream(manifest) << metadata; }

    void writePixels(const std::vector<float>& pixels, std::uint32_t width = 3,
                     std::uint32_t height = 2) const {
        std::ofstream stream(directory / "image.raw", std::ios::binary);
        const auto word = [&](std::uint32_t value) {
            const std::array<unsigned char, 4> bytes = {
                static_cast<unsigned char>(value & 255),
                static_cast<unsigned char>((value >> 8) & 255),
                static_cast<unsigned char>((value >> 16) & 255),
                static_cast<unsigned char>((value >> 24) & 255)};
            stream.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        };
        word(width);
        word(height);
        for (float pixel : pixels) word(std::bit_cast<std::uint32_t>(pixel));
    }

    void addGeolocation() {
        metadata["product"] = "geocoded_magnitude";
        metadata["geolocation"] = {{"rows", {0, 1}},
                                   {"cols", {0, 2}},
                                   {"longitude", {{-107, -105}, {-107, -105}}},
                                   {"latitude", {{35, 35}, {34, 34}}},
                                   {"interpolation_error_m", 0.002}};
    }

    void expectInvalid() {
        writeManifest();
        EXPECT_THROW(loadScientificFrame(manifest), std::runtime_error);
    }
};

TEST_F(ScientificFrameTests, ReadsFloatMagnitudesWithoutDisplayQuantization) {
    const auto frame = loadScientificFrame(manifest);
    EXPECT_EQ(frame.width, 3);
    EXPECT_EQ(frame.height, 2);
    ASSERT_EQ(frame.pixels.size(), 6);
    EXPECT_FLOAT_EQ(frame.pixels[2], 10);
    EXPECT_TRUE(std::isnan(frame.pixels[5]));
    EXPECT_FLOAT_EQ(frame.minValue, 0);
    EXPECT_FLOAT_EQ(frame.maxValue, 1000);
    EXPECT_EQ(frame.source, "source/geocoded.tif");
    EXPECT_EQ(frame.units, "uncalibrated magnitude DN");
    EXPECT_FALSE(frame.geolocation);
    EXPECT_FALSE(geolocate(frame, 0, 0));
}

TEST_F(ScientificFrameTests, PreservesSubnormalAndFractionalMagnitudes) {
    writePixels({0, std::numeric_limits<float>::denorm_min(), 0.1f, 1.25f, 20.75f, 100});
    const auto frame = loadScientificFrame(manifest);
    EXPECT_FLOAT_EQ(frame.pixels[1], std::numeric_limits<float>::denorm_min());
    EXPECT_FLOAT_EQ(frame.pixels[3], 1.25f);
}

TEST_F(ScientificFrameTests, ToneMappingUsesAmplitudeDbAndDoesNotChangeSource) {
    const auto frame = loadScientificFrame(manifest);
    EXPECT_EQ(magnitudeToGray(frame, 60), (std::vector<std::uint8_t>{0, 0, 85, 170, 255, 0}));
    EXPECT_EQ(magnitudeToGray(frame, 40), (std::vector<std::uint8_t>{0, 0, 0, 128, 255, 0}));
    EXPECT_FLOAT_EQ(frame.pixels[4], 1000);
    EXPECT_TRUE(std::isnan(frame.pixels[5]));
}

TEST_F(ScientificFrameTests, AllZeroMagnitudeDisplaysBlack) {
    writePixels({0, 0, 0, 0, 0, 0});
    const auto frame = loadScientificFrame(manifest);
    EXPECT_EQ(magnitudeToGray(frame, 50), std::vector<std::uint8_t>(6, 0));
}

TEST_F(ScientificFrameTests, ToneMappingAvoidsUnderflowForExtremeFiniteRange) {
    writePixels(
        {std::numeric_limits<float>::max(), std::numeric_limits<float>::denorm_min(), 0, 1, 2, 3});
    const auto gray = magnitudeToGray(loadScientificFrame(manifest), 200);
    EXPECT_EQ(gray[0], 255);
    EXPECT_EQ(gray[1], 0);
}

TEST_F(ScientificFrameTests, RejectsInvalidDisplayRange) {
    const auto frame = loadScientificFrame(manifest);
    for (const float range : {0.0f, -1.0f, 201.0f, std::numeric_limits<float>::infinity(),
                              std::numeric_limits<float>::quiet_NaN()}) {
        EXPECT_THROW(magnitudeToGray(frame, range), std::runtime_error);
    }
}

TEST_F(ScientificFrameTests, InterpolatesCoordinatesAtPixelCentersWithoutFlippingRows) {
    addGeolocation();
    writeManifest();
    const auto frame = loadScientificFrame(manifest);
    const auto topLeft = geolocate(frame, 0, 0);
    ASSERT_TRUE(topLeft);
    EXPECT_DOUBLE_EQ(topLeft->longitude, -107);
    EXPECT_DOUBLE_EQ(topLeft->latitude, 35);
    const auto bottomRight = geolocate(frame, 1, 2);
    ASSERT_TRUE(bottomRight);
    EXPECT_DOUBLE_EQ(bottomRight->longitude, -105);
    EXPECT_DOUBLE_EQ(bottomRight->latitude, 34);
    const auto middle = geolocate(frame, 0.5, 1);
    ASSERT_TRUE(middle);
    EXPECT_DOUBLE_EQ(middle->longitude, -106);
    EXPECT_DOUBLE_EQ(middle->latitude, 34.5);
    EXPECT_DOUBLE_EQ(middle->interpolationErrorM, 0.002);
}

TEST_F(ScientificFrameTests, HandlesNonuniformGeolocationGrid) {
    addGeolocation();
    metadata["geolocation"]["cols"] = {0, 0.25, 2};
    metadata["geolocation"]["longitude"] = {{0, 1, 8}, {0, 1, 8}};
    metadata["geolocation"]["latitude"] = {{35, 35, 35}, {34, 34, 34}};
    writeManifest();
    const auto frame = loadScientificFrame(manifest);
    EXPECT_DOUBLE_EQ(geolocate(frame, 0, 0.125)->longitude, 0.5);
    EXPECT_DOUBLE_EQ(geolocate(frame, 1, 1.125)->longitude, 4.5);
}

TEST_F(ScientificFrameTests, InterpolatesAcrossAntimeridianAlongShortArc) {
    addGeolocation();
    metadata["geolocation"]["longitude"] = {{179, -179}, {179, -179}};
    writeManifest();
    const auto frame = loadScientificFrame(manifest);
    const auto middle = geolocate(frame, 0.5, 1);
    ASSERT_TRUE(middle);
    EXPECT_DOUBLE_EQ(std::abs(middle->longitude), 180);
    EXPECT_DOUBLE_EQ(geolocate(frame, 0, 1.5)->longitude, -179.5);
}

TEST_F(ScientificFrameTests, SupportsSinglePixelGeolocationAxes) {
    metadata["product"] = "geocoded_magnitude";
    metadata["width"] = 1;
    metadata["height"] = 1;
    metadata["geolocation"] = {{"rows", {0}},
                               {"cols", {0}},
                               {"longitude", {{-106}}},
                               {"latitude", {{35}}},
                               {"interpolation_error_m", 0}};
    writePixels({5}, 1, 1);
    writeManifest();
    const auto frame = loadScientificFrame(manifest);
    ASSERT_TRUE(geolocate(frame, 0, 0));
    EXPECT_DOUBLE_EQ(geolocate(frame, 0, 0)->latitude, 35);
    EXPECT_FALSE(geolocate(frame, 0.5, 0));
}

TEST_F(ScientificFrameTests, DoesNotInventCoordinatesOutsideImage) {
    addGeolocation();
    writeManifest();
    const auto frame = loadScientificFrame(manifest);
    for (const double value : {-1.0, 3.0, std::numeric_limits<double>::infinity(),
                               std::numeric_limits<double>::quiet_NaN()}) {
        EXPECT_FALSE(geolocate(frame, value, 0));
        EXPECT_FALSE(geolocate(frame, 0, value));
    }
    EXPECT_FALSE(geolocate(frame, 1.1, 0));
    EXPECT_FALSE(geolocate(frame, 0, 2.1));
}

TEST_F(ScientificFrameTests, MissingManifestIncludesFileNameInError) {
    try {
        loadScientificFrame(directory / "missing.sarframe");
        FAIL() << "Expected load failure";
    } catch (const std::runtime_error& error) {
        EXPECT_NE(std::string(error.what()).find("missing.sarframe"), std::string::npos);
    }
}

TEST_F(ScientificFrameTests, RejectsMalformedAndOversizedJson) {
    std::ofstream(manifest) << "{not json";
    EXPECT_THROW(loadScientificFrame(manifest), std::runtime_error);
    std::ofstream(manifest) << std::string(2'000'001, ' ');
    EXPECT_THROW(loadScientificFrame(manifest), std::runtime_error);
}

TEST_F(ScientificFrameTests, RequiresVersionAndAllMandatoryMetadata) {
    const auto original = metadata;
    for (const auto& key :
         {"schema", "width", "height", "data_file", "product", "units", "source"}) {
        metadata = original;
        metadata.erase(key);
        expectInvalid();
    }
    metadata = original;
    metadata["schema"] = "unknown-v2";
    expectInvalid();
}

TEST_F(ScientificFrameTests, RejectsUnsupportedProductAndUnits) {
    metadata["product"] = "raw_echoes";
    expectInvalid();
    metadata["product"] = "scene_local_magnitude";
    metadata["units"] = "calibrated power";
    expectInvalid();
}

TEST_F(ScientificFrameTests, RequiresGeolocationOnlyForGeocodedProducts) {
    metadata["product"] = "geocoded_magnitude";
    expectInvalid();
    addGeolocation();
    metadata["product"] = "scene_local_magnitude";
    expectInvalid();
}

TEST_F(ScientificFrameTests, RejectsUnsafeOrInvalidText) {
    const auto original = metadata;
    const std::vector<nlohmann::json> invalid = {"", std::string(4097, 'x'), std::string("x\0y", 3),
                                                 42};
    for (const auto& value : invalid) {
        metadata = original;
        metadata["source"] = value;
        expectInvalid();
    }
}

TEST_F(ScientificFrameTests, RejectsInvalidAndExcessiveDimensionsBeforeAllocating) {
    const std::vector<nlohmann::json> invalid = {0, -1, 1.5, "3", 64'000'001};
    for (const auto& value : invalid) {
        metadata["width"] = value;
        expectInvalid();
    }
    metadata["width"] = 64'000'000;
    expectInvalid();  // Height 2 exceeds the total pixel budget.
}

TEST_F(ScientificFrameTests, RejectsPayloadTraversalAndAbsolutePaths) {
    for (const auto* name : {"../image.raw", "sub/image.raw", "sub\\image.raw", "C:image.raw", ".",
                             "..", "/image.raw"}) {
        metadata["data_file"] = name;
        expectInvalid();
    }
}

TEST_F(ScientificFrameTests, RejectsMissingAndNonFilePayloads) {
    metadata["data_file"] = "missing.raw";
    expectInvalid();
    std::filesystem::create_directory(directory / "folder");
    metadata["data_file"] = "folder";
    expectInvalid();
}

TEST_F(ScientificFrameTests, RejectsTruncatedTrailingAndStalePayloads) {
    writePixels({1, 2});
    EXPECT_THROW(loadScientificFrame(manifest), std::runtime_error);
    writePixels({1, 2, 3, 4, 5, 6, 7});
    EXPECT_THROW(loadScientificFrame(manifest), std::runtime_error);
    writePixels({1, 2, 3, 4, 5, 6}, 2, 3);
    EXPECT_THROW(loadScientificFrame(manifest), std::runtime_error);
    writePixels({1, 2, 3, 4, 5, 6}, 3, 9);
    EXPECT_THROW(loadScientificFrame(manifest), std::runtime_error);
}

TEST_F(ScientificFrameTests, RejectsNegativeInfiniteAndAllNodataMagnitudes) {
    for (const float invalid :
         {-1.0f, std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity()}) {
        writePixels({0, 1, 2, 3, 4, invalid});
        EXPECT_THROW(loadScientificFrame(manifest), std::runtime_error);
    }
    writePixels(std::vector<float>(6, std::numeric_limits<float>::quiet_NaN()));
    EXPECT_THROW(loadScientificFrame(manifest), std::runtime_error);
}

TEST_F(ScientificFrameTests, RejectsEmptyDuplicatePartialAndOversizedGridAxes) {
    addGeolocation();
    const auto original = metadata;
    const std::vector<nlohmann::json> invalid = {
        nlohmann::json::array(),   {0, 0, 1}, {0.1, 1}, {0, 0.5}, {0, 0.75, 0.25, 1},
        std::vector<double>(66, 0)};
    for (const auto& value : invalid) {
        metadata = original;
        metadata["geolocation"]["rows"] = value;
        expectInvalid();
    }
    metadata = original;
    metadata["geolocation"]["cols"] = "invalid";
    expectInvalid();
}

TEST_F(ScientificFrameTests, RejectsMismatchedAndOutOfBoundsCoordinates) {
    addGeolocation();
    const auto original = metadata;
    const std::vector<nlohmann::json> invalid = {
        {{1, 2}}, {{1}, {2}}, {{-181, 0}, {0, 0}}, "invalid"};
    for (const auto& value : invalid) {
        metadata = original;
        metadata["geolocation"]["longitude"] = value;
        expectInvalid();
    }
    metadata = original;
    metadata["geolocation"]["latitude"] = {{91, 0}, {0, 0}};
    expectInvalid();
    metadata = original;
    metadata["geolocation"]["latitude"] = {{nullptr, 0}, {0, 0}};
    expectInvalid();
}

TEST_F(ScientificFrameTests, RejectsInvalidInterpolationEstimate) {
    addGeolocation();
    metadata["geolocation"]["interpolation_error_m"] = -1;
    expectInvalid();
    metadata["geolocation"]["interpolation_error_m"] = nullptr;
    expectInvalid();
    metadata["geolocation"].erase("interpolation_error_m");
    expectInvalid();
}

TEST_F(ScientificFrameTests, LoadsNewMagnitudeProductsAndIgnoresOptionalExportMetadata) {
    for (const auto* product :
         {"range_profile_magnitude", "focused_magnitude", "backprojected_magnitude"}) {
        metadata["product"] = product;
        metadata["display_scale"] = "amplitude_db";
        metadata["source_shape"] = {100, 200};
        metadata["display_resampling"] = "area_average";
        writeManifest();
        const auto frame = loadScientificFrame(manifest);
        EXPECT_EQ(frame.product, product);
        EXPECT_EQ(frame.displayScale, "amplitude_db");
        EXPECT_EQ(displayToGray(frame, 60), magnitudeToGray(frame, 60));
    }
}

TEST_F(ScientificFrameTests, NonGeocodedStageCanRetainVerifiedGeolocation) {
    addGeolocation();
    metadata["product"] = "focused_magnitude";
    writeManifest();
    const auto frame = loadScientificFrame(manifest);
    ASSERT_TRUE(geolocate(frame, 0, 0));
    EXPECT_DOUBLE_EQ(geolocate(frame, 0, 0)->longitude, -107);
}

TEST_F(ScientificFrameTests, PhaseMapsFixedRadiansWithoutAmplitudeLogOrQuantizingSource) {
    constexpr float pi = std::numbers::pi_v<float>;
    const auto nodata = std::numeric_limits<float>::quiet_NaN();
    for (const auto* product : {"focused_phase", "backprojected_phase"}) {
        metadata["product"] = product;
        metadata["display_scale"] = "phase_radians";
        metadata["units"] = "radians";
        writePixels({-pi, -pi / 2, 0, pi / 2, pi, nodata});
        writeManifest();
        const auto frame = loadScientificFrame(manifest);
        EXPECT_FLOAT_EQ(frame.minValue, -pi);
        EXPECT_FLOAT_EQ(frame.maxValue, pi);
        EXPECT_FLOAT_EQ(frame.pixels[1], -pi / 2);
        EXPECT_EQ(displayToGray(frame, 60), (std::vector<std::uint8_t>{0, 64, 128, 191, 255, 0}));
        EXPECT_EQ(displayToGray(frame, -1), displayToGray(frame, 60));
        EXPECT_THROW(magnitudeToGray(frame, 60), std::runtime_error);
    }
}

TEST_F(ScientificFrameTests, AllNegativePhaseHasNegativePeakAndValidDisplay) {
    metadata["product"] = "focused_phase";
    metadata["display_scale"] = "phase_radians";
    metadata["units"] = "radians";
    writePixels({-3, -2, -1, -1.5f, -2.5f, -0.5f});
    writeManifest();
    const auto frame = loadScientificFrame(manifest);
    EXPECT_FLOAT_EQ(frame.maxValue, -0.5f);
    EXPECT_GT(displayToGray(frame, 50)[5], 0);
}

TEST_F(ScientificFrameTests, AcceptsPythonFloat32AngleEndpointsAndRejectsNextRepresentableValue) {
    // Verified np.angle(np.array([complex(-1, 0.0), complex(-1, -0.0)],
    //                          dtype=np.complex64)).astype('<f4').view('<u4').
    constexpr float positive = std::bit_cast<float>(std::uint32_t{0x40490fdb});
    constexpr float negative = std::bit_cast<float>(std::uint32_t{0xc0490fdb});
    ASSERT_GT(static_cast<double>(positive), std::numbers::pi);
    metadata["product"] = "focused_phase";
    metadata["display_scale"] = "phase_radians";
    metadata["units"] = "radians";
    writePixels({positive, negative, 0, positive, negative, 0});
    writeManifest();
    const auto frame = loadScientificFrame(manifest);
    EXPECT_EQ(displayToGray(frame, 50), (std::vector<std::uint8_t>{255, 0, 128, 255, 0, 128}));
    for (const float outside : {std::nextafter(positive, 4.0F), std::nextafter(negative, -4.0F)}) {
        writePixels({outside, 0, 0, 0, 0, 0});
        EXPECT_THROW(loadScientificFrame(manifest), std::runtime_error);
    }
}

TEST_F(ScientificFrameTests, RejectsInconsistentDisplayScaleUnitsAndPhaseBounds) {
    const auto original = metadata;
    for (const auto* scale : {"phase_radians", "log_power", ""}) {
        metadata = original;
        metadata["display_scale"] = scale;
        expectInvalid();
    }
    metadata = original;
    metadata["product"] = "focused_phase";
    metadata["units"] = "radians";
    expectInvalid();  // Default amplitude mapping must never be applied to phase.
    metadata["display_scale"] = "phase_radians";
    metadata["units"] = "degrees";
    expectInvalid();
    metadata["units"] = "radians";
    writeManifest();
    for (float invalid : {-3.15f, 3.15f, std::numeric_limits<float>::infinity()}) {
        writePixels({0, 0, 0, 0, 0, invalid});
        EXPECT_THROW(loadScientificFrame(manifest), std::runtime_error);
    }
    writePixels(std::vector<float>(6, std::numeric_limits<float>::quiet_NaN()));
    EXPECT_THROW(loadScientificFrame(manifest), std::runtime_error);
}

TEST_F(ScientificFrameTests, DisplayRejectsUnsupportedOrCorruptPhaseFrame) {
    auto frame = loadScientificFrame(manifest);
    frame.displayScale = "unsupported";
    EXPECT_THROW(displayToGray(frame, 50), std::runtime_error);
    frame.displayScale = "phase_radians";
    frame.pixels = {4};
    EXPECT_THROW(displayToGray(frame, 50), std::runtime_error);
    frame.pixels = {std::numeric_limits<float>::infinity()};
    EXPECT_THROW(displayToGray(frame, 50), std::runtime_error);
}

TEST_F(ScientificFrameTests, EnforcesCallerPixelBudgetBeforeReadingPayload) {
    EXPECT_EQ(loadScientificFrame(manifest, 6).pixels.size(), 6);
    EXPECT_THROW(loadScientificFrame(manifest, 5), std::runtime_error);
    EXPECT_THROW(loadScientificFrame(manifest, 0), std::runtime_error);
    EXPECT_THROW(loadScientificFrame(manifest, 64'000'001), std::runtime_error);
}

}  // namespace
