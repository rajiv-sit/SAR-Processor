#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "pta/TtlRunner.hpp"

namespace {

std::filesystem::path makeTempPath(const std::string& stem) {
    const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() /
           (stem + "_" + std::to_string(now));
}

}  // namespace

TEST(TtlRunnerTests, ParsesPrsConfig) {
    const auto configPath = makeTempPath("ttl_config");
    const auto reportPath = makeTempPath("pta_report.txt");
    std::ofstream output(configPath);
    ASSERT_TRUE(output);
    output << "reportPath=" << reportPath.string() << "\n";
    output << "reportFormat=text\n";
    output.close();

    pta::TtlRunner runner;
    ASSERT_TRUE(runner.runFromConfig(configPath.string()));

    EXPECT_TRUE(std::filesystem::exists(reportPath));
    std::ifstream input(reportPath);
    ASSERT_TRUE(input);
    std::string content((std::istreambuf_iterator<char>(input)),
                        std::istreambuf_iterator<char>());
    EXPECT_NE(content.find("inputConfig="), std::string::npos);
    EXPECT_NE(content.find("reportFormat=text"), std::string::npos);
}

TEST(TtlRunnerTests, WritesJsonReport) {
    const auto configPath = makeTempPath("ttl_config_json");
    const auto reportPath = makeTempPath("pta_report.json");
    std::ofstream output(configPath);
    ASSERT_TRUE(output);
    output << "reportPath=" << reportPath.string() << "\n";
    output << "reportFormat=json\n";
    output.close();

    pta::TtlRunner runner;
    ASSERT_TRUE(runner.runFromConfig(configPath.string()));

    std::ifstream input(reportPath);
    ASSERT_TRUE(input);
    nlohmann::json payload;
    input >> payload;
    EXPECT_EQ(payload.value("reportFormat", ""), "json");
    EXPECT_EQ(payload.value("inputConfig", ""), configPath.string());
}

TEST(TtlRunnerTests, ParsesJsonConfig) {
    const auto configPath = makeTempPath("ttl_config_json");
    const auto reportPath = makeTempPath("pta_report_config.json");
    std::ofstream output(configPath);
    ASSERT_TRUE(output);
    output << "{\n";
    output << "  \"reportPath\": \"" << reportPath.string() << "\",\n";
    output << "  \"reportFormat\": \"json\"\n";
    output << "}\n";
    output.close();

    pta::TtlRunner runner;
    ASSERT_TRUE(runner.runFromConfig(configPath.string()));

    std::ifstream input(reportPath);
    ASSERT_TRUE(input);
    nlohmann::json payload;
    input >> payload;
    EXPECT_EQ(payload.value("reportFormat", ""), "json");
}

TEST(TtlRunnerTests, FallsBackToTextForUnknownFormat) {
    const auto configPath = makeTempPath("ttl_config_txt");
    const auto reportPath = makeTempPath("pta_report_txt.txt");
    std::ofstream output(configPath);
    ASSERT_TRUE(output);
    output << "{\n";
    output << "  \"reportPath\": \"" << reportPath.string() << "\",\n";
    output << "  \"reportFormat\": \"unknown\"\n";
    output << "}\n";
    output.close();

    pta::TtlRunner runner;
    ASSERT_TRUE(runner.runFromConfig(configPath.string()));

    std::ifstream input(reportPath);
    ASSERT_TRUE(input);
    std::string content((std::istreambuf_iterator<char>(input)),
                        std::istreambuf_iterator<char>());
    EXPECT_NE(content.find("reportFormat=text"), std::string::npos);
}

TEST(TtlRunnerTests, FailsWhenReportPathInvalid) {
    const auto configPath = makeTempPath("ttl_bad_report");
    const auto reportPath = std::filesystem::temp_directory_path() / "no_dir" / "bad.txt";
    std::ofstream output(configPath);
    ASSERT_TRUE(output);
    output << "{\n";
    output << "  \"reportPath\": \"" << reportPath.string() << "\",\n";
    output << "  \"reportFormat\": \"text\"\n";
    output << "}\n";
    output.close();

    pta::TtlRunner runner;
    EXPECT_FALSE(runner.runFromConfig(configPath.string()));
}
