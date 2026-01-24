#include <chrono>
#include <filesystem>
#include <string>

#include <gtest/gtest.h>

#include "backproj/ImageWriter.hpp"

namespace {

std::filesystem::path makeTempPath(const std::string& stem) {
    const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() /
           (stem + "_" + std::to_string(now) + ".tif");
}

}  // namespace

TEST(ImageWriterTests, WritesTiffFile) {
    Eigen::MatrixXf image(2, 2);
    image << 1.0f, 2.0f,
             3.0f, 4.0f;

    const auto path = makeTempPath("backproj_stub");
    ASSERT_TRUE(backproj::writeTiff(path.string(), image));
    EXPECT_TRUE(std::filesystem::exists(path));
}
