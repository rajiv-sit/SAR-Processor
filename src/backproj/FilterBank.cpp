#include "backproj/FilterBank.hpp"

namespace backproj {

Eigen::VectorXf FilterBank::rangeFilter() const {
    return Eigen::VectorXf{};
}

Eigen::VectorXf FilterBank::azimuthFilter() const {
    return Eigen::VectorXf{};
}

}  // namespace backproj
