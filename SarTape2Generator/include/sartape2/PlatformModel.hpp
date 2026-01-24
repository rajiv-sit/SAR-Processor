#pragma once

#include <cstdint>

#include "sartape2/Types.hpp"

namespace sartape2 {

struct TrajectoryModel {
    Vec3 startPosition{};
    Vec3 velocity{};

    PlatformState stateAt(double timeSec) const;
};

class TimeModel {
public:
    TimeModel(double prfHz,
              double startTimeSec = 0.0,
              double jitterStdSec = 0.0,
              std::uint32_t jitterSeed = 0);

    double pulseTime(std::uint32_t pulseIndex) const;

private:
    double prfHz_ = 1.0;
    double startTimeSec_ = 0.0;
    double jitterStdSec_ = 0.0;
    std::uint32_t jitterSeed_ = 0;
};

class PlatformModel {
public:
    PlatformModel(TrajectoryModel trajectory, TimeModel time);

    PlatformState stateAtPulse(std::uint32_t pulseIndex) const;

private:
    TrajectoryModel trajectory_{};
    TimeModel time_;
};

}  // namespace sartape2
