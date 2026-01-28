#pragma once

#include <Eigen/Dense>

namespace backproj {

class IBackProjectionEngine {
public:
    virtual ~IBackProjectionEngine() = default;

    virtual void run() = 0;
    virtual Eigen::MatrixXf generateImage() = 0;
};

}  // namespace backproj
