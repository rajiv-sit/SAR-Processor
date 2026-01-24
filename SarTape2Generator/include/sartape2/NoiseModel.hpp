#pragma once

#include <cstdint>

#include "sartape2/Types.hpp"

namespace sartape2 {

class NoiseModel {
public:
    explicit NoiseModel(std::uint32_t seed = 0);

    void setNoiseStd(float stdDev);
    void setSnrDb(float snrDb, float signalRef = 1.0f);
    void apply(ComplexBuffer& buffer);

private:
    std::uint32_t seed_ = 0;
    float stdDev_ = 0.0f;
};

}  // namespace sartape2
