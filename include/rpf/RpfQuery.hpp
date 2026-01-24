#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace rpf {

struct RpfQueryResult {
    int mode = -1;
    std::vector<int> startNums;
    std::vector<int> numLines;
    std::vector<int> numPixels;
    std::vector<int> pixelTypes;
    std::vector<std::uint32_t> bofImgOffsets;
};

bool queryRpfFile(const std::string& fileName, RpfQueryResult& result);

}  // namespace rpf
