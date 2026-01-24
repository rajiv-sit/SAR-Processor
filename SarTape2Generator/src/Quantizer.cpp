#include "sartape2/Quantizer.hpp"

#include <algorithm>
#include <cmath>

namespace sartape2 {

QuantizedBuffer Quantizer::quantizeInt16(const ComplexBuffer& buffer) const {
    QuantizedBuffer out;
    out.format = SampleFormat::kInt16IQ;
    out.iq.resize(buffer.size() * 2, 0);
    float maxAbs = 0.0f;
    for (const auto& sample : buffer) {
        maxAbs = std::max(maxAbs, std::abs(sample.real()));
        maxAbs = std::max(maxAbs, std::abs(sample.imag()));
    }
    const float scale = (maxAbs > 0.0f) ? (32767.0f / maxAbs) : 1.0f;

    for (std::size_t i = 0; i < buffer.size(); ++i) {
        const auto& sample = buffer[i];
        const float iVal = std::clamp(sample.real() * scale, -32767.0f, 32767.0f);
        const float qVal = std::clamp(sample.imag() * scale, -32767.0f, 32767.0f);
        out.iq[i * 2] = static_cast<std::int16_t>(std::lround(iVal));
        out.iq[i * 2 + 1] = static_cast<std::int16_t>(std::lround(qVal));
    }
    return out;
}

QuantizedBuffer Quantizer::quantizeFloat32(const ComplexBuffer& buffer) const {
    QuantizedBuffer out;
    out.format = SampleFormat::kFloat32IQ;
    out.iqFloat.resize(buffer.size() * 2, 0.0f);
    for (std::size_t i = 0; i < buffer.size(); ++i) {
        out.iqFloat[i * 2] = buffer[i].real();
        out.iqFloat[i * 2 + 1] = buffer[i].imag();
    }
    return out;
}

}  // namespace sartape2
