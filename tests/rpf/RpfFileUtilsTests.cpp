#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

#include <gtest/gtest.h>

#include "rpf/RpfFileUtils.hpp"

namespace {

std::filesystem::path makeTempDir(const std::string& stem) {
    const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto dir = std::filesystem::temp_directory_path() /
                     (stem + "_" + std::to_string(now));
    std::filesystem::create_directories(dir);
    return dir;
}

}  // namespace

TEST(RpfFileUtilsTests, ParsesBaseFileName) {
    const std::string name = "C:\\data\\RpfAcFi1_0001.rpf";
    const std::string base = rpf::getBaseFileName(name);
    EXPECT_TRUE(base.find("RpfAcFi1_") != std::string::npos);
}

TEST(RpfFileUtilsTests, BaseFileNameWithoutUnderscore) {
    const std::string name = "C:\\data\\RpfAcFi1.rpf";
    const std::string base = rpf::getBaseFileName(name);
    EXPECT_TRUE(base.find("RpfAcFi1") != std::string::npos);
}

TEST(RpfFileUtilsTests, ParsesFileCounter) {
    const std::string name = "C:\\data\\RpfAcFi1_0012.rpf";
    EXPECT_EQ(rpf::getFileCounter(name), 12);
}

TEST(RpfFileUtilsTests, RejectsInvalidFileCounter) {
    const std::string name = "C:\\data\\RpfAcFi1_BAD.rpf";
    EXPECT_EQ(rpf::getFileCounter(name), -1);
}

TEST(RpfFileUtilsTests, FindsConsecutiveFiles) {
    const auto dir = makeTempDir("rpf_files");
    const auto base = (dir / "RpfAcFi1_").string();

    std::ofstream(dir / "RpfAcFi1_0001.rpf").put('a');
    std::ofstream(dir / "RpfAcFi1_0002.rpf").put('b');
    std::ofstream(dir / "RpfAcFi1_0004.rpf").put('c');

    const auto files = rpf::findFiles(base);
    ASSERT_EQ(files.size(), 2u);
    EXPECT_NE(files[0].find("0001.rpf"), std::string::npos);
    EXPECT_NE(files[1].find("0002.rpf"), std::string::npos);
}

TEST(RpfFileUtilsTests, SkipsNonRpfFiles) {
    const auto dir = makeTempDir("rpf_skip");
    const auto base = (dir / "RpfAcFi2_").string();

    std::ofstream(dir / "RpfAcFi2_0001.rpf").put('a');
    std::ofstream(dir / "RpfAcFi2_0002.txt").put('b');

    const auto files = rpf::findFiles(base);
    ASSERT_EQ(files.size(), 1u);
    EXPECT_NE(files[0].find("0001.rpf"), std::string::npos);
}
