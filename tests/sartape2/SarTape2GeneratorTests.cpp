#include <chrono>
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
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

TEST(SarTape2EchoTests, DefaultWindowCapturesNoiseFreeDelayedTarget) {
    sartape2::RadarParams params{};
    params.carrierFrequencyHz = 9.6e9;
    params.bandwidthHz = 20e6;
    params.pulseWidthSec = 20e-6;
    params.samplingRateHz = 40e6;
    sartape2::RadarModel radar(params);
    sartape2::WaveformModel waveform(radar, sartape2::WindowType::kHann);
    sartape2::PlatformState platform{};
    platform.position = {0.0, 0.0, 6000.0};
    sartape2::SceneModel scene;
    sartape2::Target target{};
    target.position = {0.0, 1000.0, 0.0};
    target.rcs = 1.0;
    scene.addTarget(target);
    const double delay = 2.0 * std::hypot(6000.0, 1000.0) / 299792458.0 * params.samplingRateHz;
    const auto chirp = waveform.referenceChirp();
    ASSERT_GT(delay, static_cast<double>(chirp.size()));
    sartape2::EchoSynthesizer synth;
    const auto rx = synth.synthesizePulse(platform, scene, waveform, radar);
    ASSERT_EQ(rx.size(), chirp.size() + static_cast<std::size_t>(std::ceil(delay)));
    double energy = 0.0;
    for (std::size_t i = 0; i < rx.size(); ++i) {
        if (i < static_cast<std::size_t>(std::floor(delay))) EXPECT_EQ(std::norm(rx[i]), 0.0f);
        energy += std::norm(rx[i]);
    }
    EXPECT_GT(energy, 1.0);
    const auto truncated = synth.synthesizePulse(platform, scene, waveform, radar, chirp.size());
    EXPECT_TRUE(std::all_of(truncated.begin(), truncated.end(),
                            [](auto sample) { return std::norm(sample) == 0.0f; }));
    const auto limited = synth.synthesizePulse(platform, scene, waveform, radar, rx.size() - 5);
    EXPECT_EQ(limited.size(), rx.size() - 5);
    EXPECT_TRUE(std::equal(limited.begin(), limited.end(), rx.begin()));
}

TEST(SarTape2EchoTests, HandlesEmptySceneAndZeroDelay) {
    sartape2::RadarParams params{};
    params.samplingRateHz = 10e6;
    params.pulseWidthSec = 1e-6;
    sartape2::RadarModel radar(params);
    sartape2::WaveformModel waveform(radar, sartape2::WindowType::kRect);
    sartape2::EchoSynthesizer synth;
    sartape2::SceneModel scene;
    const auto empty = synth.synthesizePulse({}, scene, waveform, radar);
    EXPECT_EQ(empty.size(), waveform.referenceChirp().size());
    EXPECT_TRUE(std::all_of(empty.begin(), empty.end(),
                            [](auto sample) { return std::norm(sample) == 0.0f; }));
    sartape2::Target target{};
    target.rcs = 1.0;
    scene.addTarget(target);
    EXPECT_EQ(synth.synthesizePulse({}, scene, waveform, radar), waveform.referenceChirp());
}

TEST(SarTape2EchoTests, RejectsNonfiniteTargetDelays) {
    sartape2::RadarParams params{};
    params.samplingRateHz = 10e6;
    params.pulseWidthSec = 1e-6;
    sartape2::RadarModel radar(params);
    sartape2::WaveformModel waveform(radar, sartape2::WindowType::kRect);
    sartape2::SceneModel scene;
    sartape2::Target target{};
    target.position.x = std::numeric_limits<double>::infinity();
    scene.addTarget(target);
    sartape2::EchoSynthesizer synth;
    EXPECT_THROW(synth.synthesizePulse({}, scene, waveform, radar), std::invalid_argument);
    EXPECT_THROW(synth.synthesizePulse({}, scene, waveform, radar, 10), std::invalid_argument);
}

TEST(SarTape2GeneratorTests, ConfiguredReceiveSamplesAreWrittenToPulse) {
    sartape2::GeneratorConfig config{};
    config.radar.samplingRateHz = 10e6;
    config.radar.pulseWidthSec = 1e-6;
    config.radar.prfHz = 1000.0;
    config.numPulses = 1;
    config.receiveSamples = 50;
    config.format = sartape2::SampleFormat::kFloat32IQ;
    sartape2::SceneModel scene;
    sartape2::Target target{};
    target.rcs = 1.0;
    scene.addTarget(target);
    sartape2::SarTape2Generator generator(config);
    generator.setScene(scene);
    const auto path = makeTempPath("sartape_receive_window");
    ASSERT_TRUE(generator.generate(path.string()));
    std::ifstream input(path, std::ios::binary);
    // SARTAPE2 v1: 122-byte header, followed by pulse index and 8 doubles.
    input.seekg(122 + 4 + 8 * 8);
    std::uint32_t scalarCount = 0;
    input.read(reinterpret_cast<char*>(&scalarCount), sizeof(scalarCount));
    ASSERT_TRUE(input);
    ASSERT_EQ(scalarCount, 2 * config.receiveSamples);
    std::vector<float> iq(scalarCount);
    input.read(reinterpret_cast<char*>(iq.data()), scalarCount * sizeof(float));
    ASSERT_TRUE(input);
    EXPECT_GT(iq.front(), 0.0f);
    EXPECT_FLOAT_EQ(iq.back(), 0.0f);
}
