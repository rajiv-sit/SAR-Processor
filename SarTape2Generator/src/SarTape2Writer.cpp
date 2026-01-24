#include "sartape2/SarTape2Writer.hpp"

namespace sartape2 {

namespace {

void writeU16(std::ofstream& output, std::uint16_t value) {
    output.write(reinterpret_cast<const char*>(&value), sizeof(value));
}

void writeU32(std::ofstream& output, std::uint32_t value) {
    output.write(reinterpret_cast<const char*>(&value), sizeof(value));
}

void writeU64(std::ofstream& output, std::uint64_t value) {
    output.write(reinterpret_cast<const char*>(&value), sizeof(value));
}

void writeDouble(std::ofstream& output, double value) {
    output.write(reinterpret_cast<const char*>(&value), sizeof(value));
}

}  // namespace

bool SarTape2Writer::open(const std::string& path,
                          const RadarModel& radar,
                          SampleFormat format,
                          const SceneExtents& extents,
                          double startTimeSec) {
    output_.open(path, std::ios::binary);
    if (!output_) {
        return false;
    }
    format_ = format;
    return writeHeader(radar, format, extents, startTimeSec);
}

void SarTape2Writer::close() {
    if (output_.is_open()) {
        output_.close();
    }
}

bool SarTape2Writer::isOpen() const {
    return output_.is_open();
}

bool SarTape2Writer::writeHeader(const RadarModel& radar,
                                 SampleFormat format,
                                 const SceneExtents& extents,
                                 double startTimeSec) {
    if (!output_) {
        return false;
    }

    const char magic[8] = {'S', 'A', 'R', 'T', 'A', 'P', 'E', '2'};
    output_.write(magic, sizeof(magic));
    writeU16(output_, 1);
    writeU16(output_, 0);
    writeU16(output_, static_cast<std::uint16_t>(format));
    writeU16(output_, radar.params().adcBits);
    const std::uint8_t endianness = 0;
    output_.write(reinterpret_cast<const char*>(&endianness), sizeof(endianness));
    std::uint8_t reserved = 0;
    output_.write(reinterpret_cast<const char*>(&reserved), sizeof(reserved));
    writeDouble(output_, radar.params().carrierFrequencyHz);
    writeDouble(output_, radar.params().bandwidthHz);
    writeDouble(output_, radar.params().pulseWidthSec);
    writeDouble(output_, radar.params().samplingRateHz);
    writeDouble(output_, radar.params().prfHz);
    writeDouble(output_, startTimeSec);
    writeDouble(output_, extents.min.x);
    writeDouble(output_, extents.min.y);
    writeDouble(output_, extents.min.z);
    writeDouble(output_, extents.max.x);
    writeDouble(output_, extents.max.y);
    writeDouble(output_, extents.max.z);
    writeU64(output_, 0);

    return static_cast<bool>(output_);
}

bool SarTape2Writer::writePulse(std::uint32_t pulseIndex,
                                const PlatformState& platform,
                                const QuantizedBuffer& buffer) {
    if (!output_) {
        return false;
    }
    writeU32(output_, pulseIndex);
    writeDouble(output_, platform.timeSec);
    writeDouble(output_, platform.timeSec);
    writeDouble(output_, platform.position.x);
    writeDouble(output_, platform.position.y);
    writeDouble(output_, platform.position.z);
    writeDouble(output_, platform.velocity.x);
    writeDouble(output_, platform.velocity.y);
    writeDouble(output_, platform.velocity.z);

    const std::uint32_t count =
        (buffer.format == SampleFormat::kFloat32IQ)
            ? static_cast<std::uint32_t>(buffer.iqFloat.size())
            : static_cast<std::uint32_t>(buffer.iq.size());
    writeU32(output_, count);
    if (buffer.format == SampleFormat::kFloat32IQ) {
        output_.write(reinterpret_cast<const char*>(buffer.iqFloat.data()),
                      static_cast<std::streamsize>(buffer.iqFloat.size() * sizeof(float)));
    } else if (buffer.format == SampleFormat::kInt16IQ) {
        output_.write(reinterpret_cast<const char*>(buffer.iq.data()),
                      static_cast<std::streamsize>(buffer.iq.size() * sizeof(std::int16_t)));
    } else {
        return false;
    }
    return static_cast<bool>(output_);
}

}  // namespace sartape2
