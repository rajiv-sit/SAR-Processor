#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "rpf/LatLongGrid.hpp"

namespace rpf {

struct RpfStreamBlock;

class IRpfProductStreamLine {
public:
    virtual ~IRpfProductStreamLine() = default;

    virtual bool readLine(int lineNum, std::vector<float>& lineData) const = 0;
    virtual bool getLatLong(int line, int pixel, double& lat, double& lon, double& grToSr) const = 0;
    virtual const LatLongGrid& latLongGrid() const = 0;
    virtual const std::vector<RpfStreamBlock>& blocks() const = 0;
};

}  // namespace rpf
