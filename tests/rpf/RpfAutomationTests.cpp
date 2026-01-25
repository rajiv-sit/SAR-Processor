#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "rpf/RpfAutomation.hpp"

namespace {

std::filesystem::path makeTempPath(const std::string& stem) {
    const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() /
           (stem + "_" + std::to_string(now) + ".json");
}

nlohmann::json readJson(const std::filesystem::path& path) {
    std::ifstream input(path);
    nlohmann::json json;
    input >> json;
    return json;
}

}  // namespace

TEST(RpfAutomationTests, WritesAnnotationReportJson) {
    const auto path = makeTempPath("rpf_report");

    rpf::AnnotationStruct annotation{};
    annotation.fileName = "sample.rpf";
    annotation.fileIdParams.radarMode = 3;
    annotation.latLongOutput.geolocationGridNumLines = 2;
    annotation.imageDataChunkHeader.dataWidth = 10;
    annotation.imageDataChunkHeader.dataHeight = 20;

    rpf::LatLongGrid grid{};
    grid.lineNumber = {1, 2};

    pta::PtaStats xStats{};
    xStats.maxPower = 5.0;
    xStats.pos = 3.0;
    pta::PtaStats yStats{};
    yStats.maxPower = 7.0;
    yStats.pos = 4.0;

    rpf::RpfAutomation automation;
    ASSERT_TRUE(automation.writeAnnotationReport(path.string(), annotation, grid, xStats, yStats));

    const auto json = readJson(path);
    EXPECT_EQ(json.value("fileName", ""), "sample.rpf");
    EXPECT_EQ(json.value("radarMode", 0), 3);
    EXPECT_EQ(json.value("geolocationGridNumLines", 0), 2);
    EXPECT_EQ(json.value("latLongGridCount", 0), 2);
    EXPECT_TRUE(json.contains("imageRect"));
    EXPECT_TRUE(json.contains("acquisition"));
    EXPECT_DOUBLE_EQ(json["ptaStats"]["x"].value("maxPower", 0.0), 5.0);
    EXPECT_DOUBLE_EQ(json["ptaStats"]["y"].value("maxPower", 0.0), 7.0);
}

TEST(RpfAutomationTests, WritesStripmapLinesJson) {
    const auto path = makeTempPath("rpf_lines");
    rpf::RpfAutomation automation;
    ASSERT_TRUE(automation.writeStripmapLinesToReprocess(path.string(), {3, 7, 9}));

    const auto json = readJson(path);
    ASSERT_TRUE(json.contains("lines"));
    EXPECT_EQ(json["lines"].size(), 3u);
    EXPECT_EQ(json.value("count", 0u), 3u);
}

TEST(RpfAutomationTests, WritesAutoPtaReportJson) {
    const auto path = makeTempPath("rpf_auto_pta");
    rpf::AnnotationStruct annotation{};
    annotation.fileName = "auto.rpf";
    annotation.fileIdParams.radarMode = 2;
    annotation.fileIdParams.fileType = 1;

    rpf::RpfAutomation automation;
    rpf::RpfAutomation::AutoPtaEntry entry{};
    entry.line = 5;
    entry.xStats.maxPower = 4.0;
    entry.yStats.maxPower = 6.0;
    ASSERT_TRUE(automation.writeAutoPtaReport(path.string(), annotation, {entry}));

    const auto json = readJson(path);
    EXPECT_EQ(json.value("fileName", ""), "auto.rpf");
    ASSERT_TRUE(json.contains("entries"));
    EXPECT_EQ(json["entries"].size(), 1u);
    EXPECT_DOUBLE_EQ(json["entries"][0]["x"].value("maxPower", 0.0), 4.0);
}

TEST(RpfAutomationTests, FailsWhenReportPathInvalid) {
    const auto path = std::filesystem::temp_directory_path() / "no_dir" / "bad.json";
    rpf::RpfAutomation automation;
    rpf::AnnotationStruct annotation{};
    rpf::LatLongGrid grid{};
    pta::PtaStats stats{};
    EXPECT_FALSE(automation.writeAnnotationReport(path.string(), annotation, grid, stats, stats));
}

TEST(RpfAutomationTests, FailsWhenStripmapPathInvalid) {
    const auto path = std::filesystem::temp_directory_path() / "no_dir" / "bad_lines.json";
    rpf::RpfAutomation automation;
    EXPECT_FALSE(automation.writeStripmapLinesToReprocess(path.string(), {1, 2}));
}
