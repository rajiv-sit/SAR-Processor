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
