#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

#include <gtest/gtest.h>

#include "sartape2/EchoSynthesizer.hpp"
#include "sartape2/NoiseModel.hpp"
#include "sartape2/Quantizer.hpp"
#include "sartape2/RadarModel.hpp"
#include "sartape2/SarTape2Generator.hpp"
#include "sartape2/SarTape2Writer.hpp"
#include "sartape2/WaveformModel.hpp"

namespace {

std::filesystem::path makeTempPath(const std::string& stem) {
    const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() /
           (stem + "_" + std::to_string(now) + ".bin");
}

}  // namespace

TEST(SarTape2WaveformTests, HannWindowTapersEdges) {
    sartape2::RadarParams params{};
    params.bandwidthHz = 10e6;
    params.pulseWidthSec = 10e-6;
    params.samplingRateHz = 20e6;
    sartape2::RadarModel radar(params);
    sartape2::WaveformModel waveform(radar, sartape2::WindowType::kHann);

    const auto chirp = waveform.referenceChirp();
    ASSERT_FALSE(chirp.empty());
    EXPECT_NEAR(chirp.front().real(), 0.0f, 1e-3f);
    EXPECT_NEAR(chirp.back().real(), 0.0f, 1e-3f);
}

TEST(SarTape2EchoTests, RangeDelayUsesSamplingRate) {
    sartape2::RadarParams params{};
    params.samplingRateHz = 10e6;
    params.carrierFrequencyHz = 9.6e9;
    sartape2::RadarModel radar(params);

    sartape2::PlatformState platform{};
    platform.position = {0.0, 0.0, 0.0};
    sartape2::TargetState target{};
    target.position = {0.0, 0.0, 1500.0};

    sartape2::RangeDelayEngine rangeDelay;
    const double delay = rangeDelay.delaySamples(platform, target, radar);
    const double expected = (2.0 * 1500.0 / 299792458.0) * params.samplingRateHz;
    EXPECT_NEAR(delay, expected, 1e-3);
}

TEST(SarTape2QuantizerTests, Float32QuantizerCopiesIQ) {
    sartape2::ComplexBuffer buffer;
    buffer.emplace_back(1.0f, -2.0f);
    buffer.emplace_back(0.5f, 0.25f);

    sartape2::Quantizer quantizer;
    auto out = quantizer.quantizeFloat32(buffer);

    ASSERT_EQ(out.iqFloat.size(), 4u);
    EXPECT_FLOAT_EQ(out.iqFloat[0], 1.0f);
    EXPECT_FLOAT_EQ(out.iqFloat[1], -2.0f);
    EXPECT_FLOAT_EQ(out.iqFloat[2], 0.5f);
    EXPECT_FLOAT_EQ(out.iqFloat[3], 0.25f);
}

TEST(SarTape2WriterTests, WritesHeaderAndPulse) {
    sartape2::RadarParams params{};
    params.carrierFrequencyHz = 9.6e9;
    params.bandwidthHz = 10e6;
    params.pulseWidthSec = 10e-6;
    params.samplingRateHz = 20e6;
    params.prfHz = 1000.0;
    sartape2::RadarModel radar(params);

    sartape2::SceneExtents extents{};
    extents.min = {-1.0, -1.0, -1.0};
    extents.max = {1.0, 1.0, 1.0};

    const auto path = makeTempPath("sartape2_writer");
    sartape2::SarTape2Writer writer;
    ASSERT_TRUE(writer.open(path.string(), radar, sartape2::SampleFormat::kFloat32IQ, extents, 0.0));

    sartape2::QuantizedBuffer buffer;
    buffer.format = sartape2::SampleFormat::kFloat32IQ;
    buffer.iqFloat = {1.0f, -1.0f};

    sartape2::PlatformState platform{};
    platform.timeSec = 0.0;
    ASSERT_TRUE(writer.writePulse(0, platform, buffer));
    writer.close();

    EXPECT_TRUE(std::filesystem::exists(path));
    EXPECT_GT(std::filesystem::file_size(path), 0u);
}

TEST(SarTape2GeneratorTests, GeneratesBinaryFile) {
    sartape2::GeneratorConfig config{};
    config.radar.carrierFrequencyHz = 9.6e9;
    config.radar.bandwidthHz = 10e6;
    config.radar.pulseWidthSec = 10e-6;
    config.radar.samplingRateHz = 20e6;
    config.radar.prfHz = 1000.0;
    config.trajectory.startPosition = {0.0, 0.0, 5000.0};
    config.trajectory.velocity = {100.0, 0.0, 0.0};
    config.numPulses = 4;
    config.window = sartape2::WindowType::kHann;

    sartape2::SceneModel scene;
    sartape2::Target target{};
    target.position = {0.0, 500.0, 0.0};
    target.rcs = 1.0;
    scene.addTarget(target);

    sartape2::SarTape2Generator generator(config);
    generator.setScene(scene);

    const auto path = makeTempPath("sartape2_generator");
    ASSERT_TRUE(generator.generate(path.string()));
    EXPECT_TRUE(std::filesystem::exists(path));
}

TEST(SarTape2GeneratorTests, GeneratesSarTapeRecords) {
    sartape2::GeneratorConfig config{};
    config.radar.carrierFrequencyHz = 9.6e9;
    config.radar.bandwidthHz = 10e6;
    config.radar.pulseWidthSec = 10e-6;
    config.radar.samplingRateHz = 20e6;
    config.radar.prfHz = 1000.0;
    config.trajectory.startPosition = {0.0, 0.0, 5000.0};
    config.trajectory.velocity = {100.0, 0.0, 0.0};
    config.numPulses = 4;

    sartape2::SceneModel scene;
    sartape2::Target target{};
    target.position = {0.0, 500.0, 0.0};
    target.rcs = 1.0;
    scene.addTarget(target);

    sartape2::SarTape2Generator generator(config);
    generator.setScene(scene);

    const auto path = makeTempPath("sartape2_ingest");
    ASSERT_TRUE(generator.generateSarTapeRecords(path.string()));
    EXPECT_TRUE(std::filesystem::exists(path));
}
