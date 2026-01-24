#include <chrono>
#include <filesystem>
#include <string>

#include <gtest/gtest.h>

#include "sar/SarTapeIngestPipeline.hpp"
#include "sartape2/SarTape2Generator.hpp"

namespace {

std::filesystem::path makeTempPath(const std::string& stem, const std::string& suffix) {
    const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() /
           (stem + "_" + std::to_string(now) + suffix);
}

}  // namespace

TEST(SarTapeIngestSyntheticTests, IngestsGeneratedRecords) {
    sartape2::GeneratorConfig config{};
    config.radar.carrierFrequencyHz = 9.6e9;
    config.radar.bandwidthHz = 10e6;
    config.radar.pulseWidthSec = 10e-6;
    config.radar.samplingRateHz = 20e6;
    config.radar.prfHz = 1000.0;
    config.trajectory.startPosition = {0.0, 0.0, 5000.0};
    config.trajectory.velocity = {100.0, 0.0, 0.0};
    config.numPulses = 8;

    sartape2::SceneModel scene;
    sartape2::Target target{};
    target.position = {0.0, 500.0, 0.0};
    target.rcs = 1.0;
    scene.addTarget(target);

    sartape2::SarTape2Generator generator(config);
    generator.setScene(scene);

    const auto inputPath = makeTempPath("sartape2_ingest", ".bin");
    ASSERT_TRUE(generator.generateSarTapeRecords(inputPath.string()));

    const auto outputPrefix = makeTempPath("sartape2_out", "");
    sar::IngestOptions options{};
    options.errorPolicy = sar::ErrorPolicy::kBestEffort;
    sar::SarTapeIngestPipeline pipeline(inputPath.string(), outputPrefix.string(), options);
    const std::uint32_t linesWritten = pipeline.run();

    EXPECT_GT(linesWritten, 0u);
    EXPECT_TRUE(std::filesystem::exists(outputPrefix.string() + ".dat"));
    EXPECT_TRUE(std::filesystem::exists(outputPrefix.string() + ".vts"));
    EXPECT_TRUE(std::filesystem::exists(outputPrefix.string() + ".hdr"));
    EXPECT_TRUE(std::filesystem::exists(outputPrefix.string() + ".ssp"));
}
