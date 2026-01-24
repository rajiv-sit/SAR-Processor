#pragma once

#include <cstddef>

#include "sartape2/SceneModel.hpp"
#include "sartape2/Types.hpp"

namespace sartape2 {

class RadarModel;
class WaveformModel;

class RangeDelayEngine {
public:
    double delaySamples(const PlatformState& platform,
                        const TargetState& target,
                        const RadarModel& radar) const;
};

class PhaseHistoryEngine {
public:
    float phaseRadians(const PlatformState& platform,
                       const TargetState& target,
                       const RadarModel& radar) const;
};

class EchoSynthesizer {
public:
    EchoSynthesizer();

    ComplexBuffer synthesizePulse(const PlatformState& platform,
                                  const SceneModel& scene,
                                  const WaveformModel& waveform,
                                  const RadarModel& radar) const;

private:
    RangeDelayEngine rangeDelay_{};
    PhaseHistoryEngine phaseHistory_{};
};

}  // namespace sartape2
