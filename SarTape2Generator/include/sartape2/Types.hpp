#pragma once

#include <array>
#include <complex>
#include <cstdint>
#include <vector>

namespace sartape2 {

using ComplexSample = std::complex<float>;
using ComplexBuffer = std::vector<ComplexSample>;

struct Vec3 {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

struct PlatformState {
    Vec3 position{};
    Vec3 velocity{};
    Vec3 attitude{};
    double timeSec = 0.0;
};

}  // namespace sartape2
