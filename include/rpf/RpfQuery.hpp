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

struct RpfAcquisitionMatch {
    std::string path;
    int mode = -1;
    int startNum = 0;
    int numLines = 0;
    int numPixels = 0;
    int pixelType = 0;
    std::uint32_t bofImgOffset = 0;
};

bool queryRpfFile(const std::string& fileName, RpfQueryResult& result);
bool queryRpfAcquisition(const std::string& fileName,
                         int frameOrLine,
                         RpfAcquisitionMatch& match,
                         std::string* error = nullptr);

}  // namespace rpf
