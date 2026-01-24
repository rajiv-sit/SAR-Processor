#pragma once

#include <cstddef>

#include "sartape2/Types.hpp"

namespace sartape2 {

class RadarModel;

enum class WindowType {
    kRect,
    kHann
};

class WaveformModel {
public:
    WaveformModel(const RadarModel& radar, WindowType window = WindowType::kRect);

    ComplexBuffer referenceChirp() const;
    std::size_t sampleCount() const;

private:
    const RadarModel& radar_;
    WindowType window_ = WindowType::kRect;
};

}  // namespace sartape2
