#include "sartape2/PlatformModel.hpp"

#include <random>

namespace sartape2 {

PlatformState TrajectoryModel::stateAt(double timeSec) const {
    PlatformState state{};
    state.timeSec = timeSec;
    state.position.x = startPosition.x + velocity.x * timeSec;
    state.position.y = startPosition.y + velocity.y * timeSec;
    state.position.z = startPosition.z + velocity.z * timeSec;
    state.velocity = velocity;
    return state;
}

TimeModel::TimeModel(double prfHz,
                     double startTimeSec,
                     double jitterStdSec,
                     std::uint32_t jitterSeed)
    : prfHz_(prfHz),
      startTimeSec_(startTimeSec),
      jitterStdSec_(jitterStdSec),
      jitterSeed_(jitterSeed) {}

double TimeModel::pulseTime(std::uint32_t pulseIndex) const {
    if (prfHz_ <= 0.0) {
        return startTimeSec_;
    }
    double time = startTimeSec_ + static_cast<double>(pulseIndex) / prfHz_;
    if (jitterStdSec_ > 0.0) {
        std::mt19937 rng(jitterSeed_ ^ pulseIndex);
        std::normal_distribution<double> dist(0.0, jitterStdSec_);
        time += dist(rng);
    }
    return time;
}

PlatformModel::PlatformModel(TrajectoryModel trajectory, TimeModel time)
    : trajectory_(trajectory), time_(time) {}

PlatformState PlatformModel::stateAtPulse(std::uint32_t pulseIndex) const {
    const double timeSec = time_.pulseTime(pulseIndex);
    return trajectory_.stateAt(timeSec);
}

}  // namespace sartape2
