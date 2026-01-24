#include "sartape2/NoiseModel.hpp"

#include <cmath>
#include <random>

namespace sartape2 {

NoiseModel::NoiseModel(std::uint32_t seed) : seed_(seed) {}

void NoiseModel::setNoiseStd(float stdDev) {
    stdDev_ = stdDev;
}

void NoiseModel::setSnrDb(float snrDb, float signalRef) {
    if (snrDb <= 0.0f || signalRef <= 0.0f) {
        stdDev_ = 0.0f;
        return;
    }
    const float ratio = std::pow(10.0f, snrDb / 20.0f);
    stdDev_ = signalRef / ratio;
}

void NoiseModel::apply(ComplexBuffer& buffer) {
    if (stdDev_ <= 0.0f) {
        return;
    }
    std::mt19937 rng(seed_);
    std::normal_distribution<float> dist(0.0f, stdDev_);
    for (auto& sample : buffer) {
        sample += ComplexSample(dist(rng), dist(rng));
    }
}

}  // namespace sartape2
