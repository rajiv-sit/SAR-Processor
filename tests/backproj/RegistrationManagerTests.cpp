#include <filesystem>

#include <gtest/gtest.h>

#include <Eigen/Core>

#include "backproj/RegistrationManager.hpp"

TEST(RegistrationManagerTests, AppliesShiftWhenEnabled) {
    backproj::RegistrationManager manager;
    backproj::FrameRegistrationParams params{};
    params.preShiftImageGrid = true;
    manager.configure(params);

    Eigen::MatrixXf image1 = Eigen::MatrixXf::Zero(2, 2);
    image1(0, 0) = 1.0f;
    manager.registerFrame(image1, true);

    Eigen::MatrixXf image2 = Eigen::MatrixXf::Zero(2, 2);
    image2(0, 1) = 1.0f;
    const auto result = manager.registerFrame(image2, true);
    EXPECT_EQ(result.dx, -1);
    EXPECT_EQ(result.dy, 0);
    EXPECT_FLOAT_EQ(image2(0, 0), 1.0f);
}

TEST(RegistrationManagerTests, AppliesShiftWithOutOfBoundsOffsets) {
    backproj::RegistrationManager manager;
    backproj::FrameRegistrationParams params{};
    params.preShiftImageGrid = true;
    manager.configure(params);

    Eigen::MatrixXf reference = Eigen::MatrixXf::Zero(2, 2);
    reference(0, 0) = 1.0f;
    manager.registerFrame(reference, true);

    Eigen::MatrixXf shifted = Eigen::MatrixXf::Zero(2, 2);
    shifted(1, 1) = 2.0f;
    const auto result = manager.registerFrame(shifted, true);
    EXPECT_EQ(result.dx, -1);
    EXPECT_EQ(result.dy, -1);
    EXPECT_FLOAT_EQ(shifted(0, 0), 2.0f);
    EXPECT_FLOAT_EQ(shifted(1, 1), 0.0f);
}

TEST(RegistrationManagerTests, DoesNotShiftWhenDisabled) {
    backproj::RegistrationManager manager;
    backproj::FrameRegistrationParams params{};
    params.preShiftImageGrid = true;
    manager.configure(params);

    Eigen::MatrixXf image1 = Eigen::MatrixXf::Zero(2, 2);
    image1(0, 0) = 1.0f;
    manager.registerFrame(image1, true);

    Eigen::MatrixXf image2 = Eigen::MatrixXf::Zero(2, 2);
    image2(0, 1) = 1.0f;
    manager.registerFrame(image2, false);
    EXPECT_FLOAT_EQ(image2(0, 1), 1.0f);
}

TEST(RegistrationManagerTests, ZeroEnergyFrameKeepsDefaults) {
    backproj::RegistrationManager manager;
    backproj::FrameRegistrationParams params{};
    params.preShiftImageGrid = true;
    manager.configure(params);

    Eigen::MatrixXf image = Eigen::MatrixXf::Zero(2, 2);
    const auto result = manager.registerFrame(image, true);
    EXPECT_EQ(result.dx, 0);
    EXPECT_EQ(result.dy, 0);
    ASSERT_EQ(manager.results().size(), 1u);
    EXPECT_EQ(manager.results().front().frameIndex, 0);
}

TEST(RegistrationManagerTests, ZeroShiftLeavesImageUnchanged) {
    backproj::RegistrationManager manager;
    backproj::FrameRegistrationParams params{};
    params.preShiftImageGrid = true;
    manager.configure(params);

    Eigen::MatrixXf image = Eigen::MatrixXf::Zero(1, 2);
    image(0, 0) = 3.0f;
    manager.registerFrame(image, true);

    Eigen::MatrixXf same = Eigen::MatrixXf::Zero(1, 2);
    same(0, 0) = 3.0f;
    const auto result = manager.registerFrame(same, true);
    EXPECT_EQ(result.dx, 0);
    EXPECT_EQ(result.dy, 0);
    EXPECT_FLOAT_EQ(same(0, 0), 3.0f);
}

TEST(RegistrationManagerTests, ResetClearsState) {
    backproj::RegistrationManager manager;
    Eigen::MatrixXf image = Eigen::MatrixXf::Zero(1, 1);
    image(0, 0) = 1.0f;
    manager.registerFrame(image, false);
    manager.reset();

    const auto result = manager.registerFrame(image, false);
    EXPECT_EQ(result.dx, 0);
    EXPECT_EQ(result.dy, 0);
}

TEST(RegistrationManagerTests, SaveJsonFailsForInvalidPath) {
    backproj::RegistrationManager manager;
    const auto path = std::filesystem::temp_directory_path() / "no_dir" / "reg.json";
    EXPECT_FALSE(manager.saveJson(path.string()));
}

TEST(RegistrationManagerTests, SaveJsonWritesFile) {
    backproj::RegistrationManager manager;
    Eigen::MatrixXf image = Eigen::MatrixXf::Zero(1, 1);
    image(0, 0) = 1.0f;
    manager.registerFrame(image, false);

    const auto path = std::filesystem::temp_directory_path() / "reg_results.json";
    EXPECT_TRUE(manager.saveJson(path.string()));
    EXPECT_TRUE(std::filesystem::exists(path));
}
