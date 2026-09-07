#include "sartape2/SarTape2Generator.hpp"

#include <algorithm>

#include "sartape2/SarTapeRecordWriter.hpp"

namespace sartape2 {

SarTape2Generator::SarTape2Generator(GeneratorConfig config)
    : config_(config),
      radar_(config.radar),
      waveform_(radar_, config.window),
      rfChain_(),
      platform_(config.trajectory,
                TimeModel(config.radar.prfHz,
                          config.startTimeSec,
                          config.prfJitterStdSec,
                          config.prfJitterSeed)),
      scene_(),
      synthesizer_(),
      noise_(0),
      quantizer_(),
      writer_() {
    if (config.snrDb > 0.0f) {
        noise_.setSnrDb(config.snrDb, config.signalRef);
    } else {
        noise_.setNoiseStd(config.noiseStd);
    }
}

void SarTape2Generator::setScene(SceneModel scene) {
    scene_ = std::move(scene);
}

bool SarTape2Generator::generate(const std::string& outputPath) {
    if (config_.numPulses == 0) {
        return false;
    }

    SceneExtents extents{};
    bool hasTarget = false;
    for (const auto& target : scene_.targetsAt(config_.startTimeSec)) {
        if (!hasTarget) {
            extents.min = target.position;
            extents.max = target.position;
            hasTarget = true;
        } else {
            extents.min.x = std::min(extents.min.x, target.position.x);
            extents.min.y = std::min(extents.min.y, target.position.y);
            extents.min.z = std::min(extents.min.z, target.position.z);
            extents.max.x = std::max(extents.max.x, target.position.x);
            extents.max.y = std::max(extents.max.y, target.position.y);
            extents.max.z = std::max(extents.max.z, target.position.z);
        }
    }

    if (!writer_.open(outputPath, radar_, config_.format, extents, config_.startTimeSec)) {
        return false;
    }

    for (std::uint32_t pulse = 0; pulse < config_.numPulses; ++pulse) {
        PlatformState platform = platform_.stateAtPulse(pulse);
        ComplexBuffer rx = synthesizer_.synthesizePulse(platform, scene_, waveform_, radar_,
                                                        config_.receiveSamples);
        noise_.apply(rx);
        rfChain_.applyGain(rx, 1.0f);
        QuantizedBuffer q = (config_.format == SampleFormat::kFloat32IQ)
            ? quantizer_.quantizeFloat32(rx)
            : quantizer_.quantizeInt16(rx);
        if (!writer_.writePulse(pulse, platform, q)) {
            writer_.close();
            return false;
        }
    }

    writer_.close();
    return true;
}

bool SarTape2Generator::generateSarTapeRecords(const std::string& outputPath) {
    if (config_.numPulses == 0) {
        return false;
    }
    SarTapeRecordWriter writer;
    if (!writer.open(outputPath)) {
        return false;
    }

    const std::uint16_t sceneNumber = 1;
    writer.writeSceneHeader(sceneNumber, 0);

    for (std::uint32_t pulse = 0; pulse < config_.numPulses; ++pulse) {
        PlatformState platform = platform_.stateAtPulse(pulse);
        ComplexBuffer rx = synthesizer_.synthesizePulse(platform, scene_, waveform_, radar_,
                                                        config_.receiveSamples);
        noise_.apply(rx);
        rfChain_.applyGain(rx, 1.0f);
        QuantizedBuffer q = quantizer_.quantizeInt16(rx);
        if (!writer.writeDataRecord(sceneNumber,
                                    static_cast<std::uint16_t>(pulse + 1),
                                    static_cast<std::uint32_t>(pulse),
                                    q.iq)) {
            writer.close();
            return false;
        }
    }

    writer.close();
    return true;
}

}  // namespace sartape2
