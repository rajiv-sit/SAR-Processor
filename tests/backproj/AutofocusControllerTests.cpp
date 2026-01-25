#include <filesystem>

#include <gtest/gtest.h>

#include <Eigen/Core>

#include "backproj/AutofocusController.hpp"

TEST(AutofocusControllerTests, HandlesEmptyImage) {
    backproj::AutofocusController controller;
    Eigen::MatrixXf image;
    const auto result = controller.analyzeFrame(image);
    EXPECT_FALSE(result.isBest);
    EXPECT_DOUBLE_EQ(result.focusMetric, 0.0);
}

TEST(AutofocusControllerTests, TracksBestMetric) {
    backproj::AutofocusController controller;
    Eigen::MatrixXf image(2, 2);
    image << 0.0f, 1.0f,
             0.0f, 2.0f;
    const auto first = controller.analyzeFrame(image);
    EXPECT_TRUE(first.isBest);

    Eigen::MatrixXf lower(2, 2);
    lower << 0.0f, 0.0f,
             0.0f, 0.0f;
    const auto second = controller.analyzeFrame(lower);
    EXPECT_FALSE(second.isBest);
}

TEST(AutofocusControllerTests, ResetClearsResults) {
    backproj::AutofocusController controller;
    Eigen::MatrixXf image(2, 2);
    image << 0.0f, 1.0f,
             0.0f, 2.0f;
    controller.analyzeFrame(image);
    controller.reset();
    const auto result = controller.analyzeFrame(image);
    EXPECT_TRUE(result.isBest);
}

TEST(AutofocusControllerTests, SaveJsonFailsForInvalidPath) {
    backproj::AutofocusController controller;
    const auto path = std::filesystem::temp_directory_path() / "no_dir" / "auto.json";
    EXPECT_FALSE(controller.saveJson(path.string()));
}
