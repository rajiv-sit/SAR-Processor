#pragma once

#include <string>

#include <Eigen/Core>

#include "backproj/BackProjSecondaryConfig.hpp"

namespace backproj {

bool writeTiff(const std::string& path, const Eigen::MatrixXf& image);
bool writeNormalizedTiff(const std::string& path,
                         const Eigen::MatrixXf& image,
                         const ImageScalingParams& scaling);
bool writeRawFloat(const std::string& path, const Eigen::MatrixXf& image);
bool readRawFloat(const std::string& path, Eigen::MatrixXf& image);

}  // namespace backproj
