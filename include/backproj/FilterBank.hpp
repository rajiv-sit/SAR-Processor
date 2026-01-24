#pragma once

#include <Eigen/Core>

#include "backproj/BackProjSecondaryConfig.hpp"

namespace backproj {

class FilterBank {
public:
    FilterBank(FilterParams rangeParams, FilterParams azimuthParams);

    Eigen::VectorXf rangeWindow(std::size_t size) const;
    Eigen::VectorXf azimuthWindow(std::size_t size) const;
    static void applyWindow(Eigen::MatrixXf& image,
                            const Eigen::VectorXf& window,
                            bool alongColumns);

private:
    FilterParams rangeParams_;
    FilterParams azimuthParams_;
};

}  // namespace backproj
