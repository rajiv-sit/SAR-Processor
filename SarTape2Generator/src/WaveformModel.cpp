#include "sartape2/WaveformModel.hpp"

#include <cmath>
#include <numbers>

#include "sartape2/RadarModel.hpp"

namespace sartape2 {

WaveformModel::WaveformModel(const RadarModel& radar, WindowType window)
    : radar_(radar), window_(window) {}

std::size_t WaveformModel::sampleCount() const {
    const auto& params = radar_.params();
    if (params.pulseWidthSec <= 0.0 || params.samplingRateHz <= 0.0) {
        return 0;
    }
    return static_cast<std::size_t>(params.pulseWidthSec * params.samplingRateHz);
}

ComplexBuffer WaveformModel::referenceChirp() const {
    ComplexBuffer buffer;
    const auto& params = radar_.params();
    const std::size_t count = sampleCount();
    buffer.resize(count, ComplexSample(0.0f, 0.0f));
    if (count == 0) {
        return buffer;
    }

    const double k = (params.pulseWidthSec > 0.0)
        ? (params.bandwidthHz / params.pulseWidthSec)
        : 0.0;
    const double denom = (count > 1) ? static_cast<double>(count - 1) : 1.0;
    for (std::size_t i = 0; i < count; ++i) {
        const double t = static_cast<double>(i) / params.samplingRateHz;
        const double phase = std::numbers::pi_v<double> * k * t * t;
        float windowValue = 1.0f;
        if (window_ == WindowType::kHann) {
            windowValue = static_cast<float>(0.5 - 0.5 * std::cos(2.0 * std::numbers::pi_v<double> *
                                                                  static_cast<double>(i) / denom));
        }
        buffer[i] = std::polar(windowValue, static_cast<float>(phase));
    }
    return buffer;
}

}  // namespace sartape2
