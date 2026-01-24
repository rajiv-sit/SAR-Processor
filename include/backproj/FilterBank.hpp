#pragma once

#include <Eigen/Core>

namespace backproj {

class FilterBank {
public:
    Eigen::VectorXf rangeFilter() const;
    Eigen::VectorXf azimuthFilter() const;
};

}  // namespace backproj
