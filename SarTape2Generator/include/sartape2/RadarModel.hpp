#pragma once

#include <cstdint>

namespace sartape2 {

struct RadarParams {
    double carrierFrequencyHz = 0.0;
    double bandwidthHz = 0.0;
    double pulseWidthSec = 0.0;
    double samplingRateHz = 0.0;
    double prfHz = 0.0;
    std::uint16_t adcBits = 16;
};

class RadarModel {
public:
    explicit RadarModel(RadarParams params);

    const RadarParams& params() const { return params_; }
    double wavelengthMeters() const;

private:
    RadarParams params_{};
};

}  // namespace sartape2
