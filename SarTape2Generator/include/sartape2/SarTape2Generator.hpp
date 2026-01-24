#pragma once

#include <cstdint>
#include <string>

#include "sartape2/EchoSynthesizer.hpp"
#include "sartape2/NoiseModel.hpp"
#include "sartape2/PlatformModel.hpp"
#include "sartape2/Quantizer.hpp"
#include "sartape2/RFChainModel.hpp"
#include "sartape2/RadarModel.hpp"
#include "sartape2/SarTape2Writer.hpp"
#include "sartape2/SceneModel.hpp"
#include "sartape2/WaveformModel.hpp"
#include "sartape2/WaveformModel.hpp"

namespace sartape2 {

struct GeneratorConfig {
    RadarParams radar{};
    TrajectoryModel trajectory{};
    WindowType window = WindowType::kRect;
    double startTimeSec = 0.0;
    double prfJitterStdSec = 0.0;
    std::uint32_t prfJitterSeed = 0;
    std::uint32_t numPulses = 0;
    float noiseStd = 0.0f;
    float snrDb = 0.0f;
    float signalRef = 1.0f;
    SampleFormat format = SampleFormat::kInt16IQ;
};

class SarTape2Generator {
public:
    explicit SarTape2Generator(GeneratorConfig config);

    void setScene(SceneModel scene);
    bool generate(const std::string& outputPath);
    bool generateSarTapeRecords(const std::string& outputPath);

private:
    GeneratorConfig config_{};
    RadarModel radar_;
    WaveformModel waveform_;
    RFChainModel rfChain_;
    PlatformModel platform_;
    SceneModel scene_;
    EchoSynthesizer synthesizer_;
    NoiseModel noise_;
    Quantizer quantizer_;
    SarTape2Writer writer_;
};

}  // namespace sartape2
