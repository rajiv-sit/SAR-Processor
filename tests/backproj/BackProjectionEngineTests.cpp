#include <chrono>
#include <filesystem>
#include <string>

#include <gtest/gtest.h>

#include "backproj/BackProjectionEngine.hpp"

namespace {

std::filesystem::path makeTempPrefix(const std::string& stem) {
    const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() /
           (stem + "_" + std::to_string(now));
}

}  // namespace

TEST(BackProjectionEngineTests, WritesStubOutputWhenConfigured) {
    backproj::BackProjOperatorConfig op{};
    op.nPixX = 4;
    op.nPixY = 3;
    const auto prefix = makeTempPrefix("backproj");
    op.rpfBaseFileName = prefix.string();

    backproj::BackProjSecondaryConfig secondary{};
    secondary.rngFilterParams.windowCoef = 0.54;
    secondary.azmFilterParams.windowCoef = 0.54;

    backproj::BackProjectionEngine engine(op, secondary);
    engine.run();

    const auto path = prefix.string() + "_stub.tif";
    EXPECT_TRUE(std::filesystem::exists(path));
    EXPECT_TRUE(std::filesystem::exists(prefix.string() + "_registration.json"));
    EXPECT_TRUE(std::filesystem::exists(prefix.string() + "_autofocus.json"));
}

TEST(BackProjectionEngineTests, SkipsWorkWhenPixelDimsZero) {
    const auto tempDir = std::filesystem::temp_directory_path() / "backproj_empty";
    std::filesystem::create_directories(tempDir);
    const auto prev = std::filesystem::current_path();
    std::filesystem::current_path(tempDir);

    backproj::BackProjOperatorConfig op{};
    op.nPixX = 0;
    op.nPixY = 4;
    backproj::BackProjSecondaryConfig secondary{};

    backproj::BackProjectionEngine engine(op, secondary);
    engine.run();

    EXPECT_FALSE(std::filesystem::exists("backproj_stub.tif"));
    std::filesystem::current_path(prev);
}

TEST(BackProjectionEngineTests, SkipsRegistrationAndAutofocusWhenDisabled) {
    backproj::BackProjOperatorConfig op{};
    op.nPixX = 2;
    op.nPixY = 2;
    op.applyAutoFocus = false;
    op.applyFrameRegistration = false;
    const auto prefix = makeTempPrefix("backproj_disabled");
    op.rpfBaseFileName = prefix.string();

    backproj::BackProjSecondaryConfig secondary{};

    backproj::BackProjectionEngine engine(op, secondary);
    engine.run();

    EXPECT_TRUE(std::filesystem::exists(prefix.string() + "_stub.tif"));
    EXPECT_FALSE(std::filesystem::exists(prefix.string() + "_registration.json"));
    EXPECT_FALSE(std::filesystem::exists(prefix.string() + "_autofocus.json"));
}
