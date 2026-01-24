#pragma once

#include <string>

#include <Eigen/Core>

namespace backproj {

bool writeTiff(const std::string& path, const Eigen::MatrixXf& image);

}  // namespace backproj
