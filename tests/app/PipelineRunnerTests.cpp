#include <cstdlib>
#include <filesystem>
#include <string>
#include <type_traits>

#include <gtest/gtest.h>

#include <Eigen/Dense>

#include "app/PipelineRunner.hpp"
#include "backproj/BackProjectionEngine.hpp"
#include "backproj/IBackProjectionEngine.hpp"
#include "pta/IPtaAnalyzer.hpp"
#include "pta/PtaAnalyzer.hpp"
#include "rpf/IRpfProductStreamLine.hpp"
#include "rpf/RpfProductStreamLine.hpp"
#include "rpf/RpfWriter.hpp"
#include "sar/ISarTapeReader.hpp"
#include "sar/SarTapeReader.hpp"

namespace {

void setEnv(const std::string& name, const std::string& value) {
#ifdef _WIN32
    _putenv_s(name.c_str(), value.c_str());
#else
    setenv(name.c_str(), value.c_str(), 1);
#endif
}

rpf::LatLongGrid buildFlatGrid(std::uint32_t rows, std::uint16_t lines) {
    if (lines < 2) {
        lines = 2;
    }
    rpf::LatLongGrid grid{};
    grid.lineNumber.assign(lines, 0);
    grid.beginGrSrRatio.assign(lines, 1.0);
    grid.midGrSrRatio.assign(lines, 1.0);
    grid.endGrSrRatio.assign(lines, 1.0);
    grid.beginLatitude.assign(lines, 0.0);
    grid.beginLongitude.assign(lines, 0.0);
    grid.midLatitude.assign(lines, 0.0);
    grid.midLongitude.assign(lines, 0.0);
    grid.endLatitude.assign(lines, 0.0);
    grid.endLongitude.assign(lines, 0.0);
    const std::uint32_t step = rows / lines;
    for (std::uint16_t i = 0; i < lines; ++i) {
        grid.lineNumber[i] = static_cast<int>(1 + i * std::max(1u, step));
    }
    return grid;
}

}  // namespace

static_assert(std::is_base_of_v<sar::ISarTapeReader, sar::SarTapeReader>);
static_assert(std::is_base_of_v<rpf::IRpfProductStreamLine, rpf::RpfProductStreamLine>);
static_assert(std::is_base_of_v<backproj::IBackProjectionEngine, backproj::BackProjectionEngine>);
static_assert(std::is_base_of_v<pta::IPtaAnalyzer, pta::PtaAnalyzer>);

TEST(PipelineRunnerTests, RtoPreviewFailsWithInvalidEndpoint) {
    app::PipelineRunner runner;
    EXPECT_FALSE(runner.runRtoPreview("udp://", 8, 8, 1, 0));
}

TEST(PipelineRunnerTests, RtoPreviewPublishesFrameWithDefaults) {
    app::PipelineRunner runner;
    EXPECT_TRUE(runner.runRtoPreview("udp://127.0.0.1:5005", 0, 0, 0, 0));
}

TEST(PipelineRunnerTests, RunPipelineWithSmallRpf) {
    const auto tempRoot = std::filesystem::temp_directory_path() / "sar_pipeline_test";
    std::filesystem::remove_all(tempRoot);
    std::filesystem::create_directories(tempRoot);

    const auto rpfPath = tempRoot / "small.rpf";
    Eigen::MatrixXf image(16, 16);
    for (int row = 0; row < image.rows(); ++row) {
        for (int col = 0; col < image.cols(); ++col) {
            image(row, col) = static_cast<float>(row * image.cols() + col);
        }
    }

    rpf::RpfWriteOptions options{};
    options.pixelType = 2;
    options.radarMode = 1;
    options.geolocationGridNumLines = 2;
    std::string error;
    const auto grid = buildFlatGrid(static_cast<std::uint32_t>(image.rows()),
                                    options.geolocationGridNumLines);
    ASSERT_TRUE(rpf::writeRpfFile(rpfPath.string(), image, options, grid, error)) << error;

    setEnv("SAR_PIPELINE_RPF", rpfPath.string());
    setEnv("SAR_PIPELINE_OUTPUT_DIR", (tempRoot / "out").string());
    setEnv("SAR_PIPELINE_PTA_CHIP", "8");

    app::PipelineRunner runner;
    EXPECT_TRUE(runner.run());
    EXPECT_TRUE(std::filesystem::exists(tempRoot / "out" / "pta_report.json"));
}
