#pragma once

#include <vector>

namespace rpf {

struct LatLongGrid {
    std::vector<int> lineNumber;
    std::vector<double> beginGrSrRatio;
    std::vector<double> midGrSrRatio;
    std::vector<double> endGrSrRatio;
    std::vector<double> beginLatitude;
    std::vector<double> beginLongitude;
    std::vector<double> midLatitude;
    std::vector<double> midLongitude;
    std::vector<double> endLatitude;
    std::vector<double> endLongitude;
};

}  // namespace rpf
