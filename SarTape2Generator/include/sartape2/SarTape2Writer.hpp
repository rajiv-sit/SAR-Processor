#pragma once

#include <cstdint>
#include <fstream>
#include <string>

#include "sartape2/Quantizer.hpp"
#include "sartape2/RadarModel.hpp"
#include "sartape2/Types.hpp"

namespace sartape2 {

struct SceneExtents {
    Vec3 min{};
    Vec3 max{};
};

class SarTape2Writer {
public:
    bool open(const std::string& path,
              const RadarModel& radar,
              SampleFormat format,
              const SceneExtents& extents,
              double startTimeSec);
    void close();
    bool isOpen() const;

    bool writePulse(std::uint32_t pulseIndex,
                    const PlatformState& platform,
                    const QuantizedBuffer& buffer);

private:
    bool writeHeader(const RadarModel& radar,
                     SampleFormat format,
                     const SceneExtents& extents,
                     double startTimeSec);

    std::ofstream output_;
    SampleFormat format_ = SampleFormat::kInt16IQ;
};

}  // namespace sartape2
