#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "rpf/LatLongGrid.hpp"

namespace rpf {

struct RpfStreamBlock {
    std::string path;
    int startLine = 0;
    int numLines = 0;
    int numPixels = 0;
    int pixelType = 0;
    std::uint32_t bofImgOffset = 0;
};

class RpfProductStreamLine {
public:
    static bool init(const std::string& fileName,
                     int frameNum,
                     RpfProductStreamLine& stream,
                     std::string& error);
    static RpfProductStreamLine makeSynthetic(const std::vector<RpfStreamBlock>& blocks,
                                             const LatLongGrid& grid);

    bool readLine(int lineNum, std::vector<float>& lineData) const;
    bool getLatLong(int line, int pixel, double& lat, double& lon, double& grToSr) const;

    const LatLongGrid& latLongGrid() const { return latLongGrid_; }
    const std::vector<RpfStreamBlock>& blocks() const { return blocks_; }

private:
    std::vector<RpfStreamBlock> blocks_;
    LatLongGrid latLongGrid_;
};

}  // namespace rpf
