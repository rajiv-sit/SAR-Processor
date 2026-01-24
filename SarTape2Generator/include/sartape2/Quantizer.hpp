#pragma once

#include <cstdint>
#include <vector>

#include "sartape2/Types.hpp"

namespace sartape2 {

enum class SampleFormat {
    kInt16IQ = 1,
    kFloat32IQ = 2
};

struct QuantizedBuffer {
    SampleFormat format = SampleFormat::kInt16IQ;
    std::vector<std::int16_t> iq;
    std::vector<float> iqFloat;
};

class Quantizer {
public:
    QuantizedBuffer quantizeInt16(const ComplexBuffer& buffer) const;
    QuantizedBuffer quantizeFloat32(const ComplexBuffer& buffer) const;
};

}  // namespace sartape2
