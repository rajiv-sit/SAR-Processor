#include "sartape2/RadarModel.hpp"

namespace sartape2 {

namespace {
constexpr double kSpeedOfLight = 299792458.0;
}

RadarModel::RadarModel(RadarParams params) : params_(params) {}

double RadarModel::wavelengthMeters() const {
    if (params_.carrierFrequencyHz <= 0.0) {
        return 0.0;
    }
    return kSpeedOfLight / params_.carrierFrequencyHz;
}

}  // namespace sartape2
