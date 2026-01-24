#include <filesystem>
#include <iostream>
#include <string>

#include "sartape2/SarTape2Generator.hpp"

namespace {

sartape2::GeneratorConfig defaultConfig() {
    sartape2::GeneratorConfig config{};
    config.radar.carrierFrequencyHz = 9.6e9;
    config.radar.bandwidthHz = 20e6;
    config.radar.pulseWidthSec = 20e-6;
    config.radar.samplingRateHz = 40e6;
    config.radar.prfHz = 1000.0;
    config.radar.adcBits = 16;
    config.trajectory.startPosition = {0.0, 0.0, 6000.0};
    config.trajectory.velocity = {120.0, 0.0, 0.0};
    config.startTimeSec = 0.0;
    config.numPulses = 128;
    config.noiseStd = 0.001f;
    config.window = sartape2::WindowType::kHann;
    config.format = sartape2::SampleFormat::kInt16IQ;
    return config;
}

sartape2::SceneModel defaultScene() {
    sartape2::SceneModel scene;
    sartape2::Target target{};
    target.position = {0.0, 1000.0, 0.0};
    target.velocity = {0.0, 0.0, 0.0};
    target.rcs = 1.0;
    scene.addTarget(target);
    return scene;
}

}  // namespace

int main(int argc, char** argv) {
    std::filesystem::path outputPath = "SarTape2Generator/output/sartape2_sample.bin";
    if (argc > 1) {
        outputPath = argv[1];
    }

    std::filesystem::create_directories(outputPath.parent_path());

    sartape2::SarTape2Generator generator(defaultConfig());
    generator.setScene(defaultScene());

    if (!generator.generate(outputPath.string())) {
        std::cerr << "Failed to generate SarTape2 data at " << outputPath << "\n";
        return 1;
    }

    std::cout << "Generated SarTape2 data at " << outputPath << "\n";

    const auto ingestPath = outputPath.parent_path() / "sartape2_ingest_sample.bin";
    if (!generator.generateSarTapeRecords(ingestPath.string())) {
        std::cerr << "Failed to generate ingest sample at " << ingestPath << "\n";
        return 1;
    }
    std::cout << "Generated ingest sample at " << ingestPath << "\n";
    return 0;
}
