#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

#include <gtest/gtest.h>

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
}
