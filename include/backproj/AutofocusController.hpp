#pragma once

#include <string>
#include <vector>

#include <Eigen/Core>

#include "backproj/AutofocusParams.hpp"

namespace backproj {

struct AutofocusPoint {
    int row = 0;
    int col = 0;
    float value = 0.0f;
};

struct AutofocusResult {
    int frameIndex = 0;
    double focusMetric = 0.0;
    double metricDelta = 0.0;
    bool isBest = false;
    bool converged = false;
    std::string selectionMethod;
    std::vector<AutofocusPoint> selectedPoints;
};

class AutofocusController {
public:
    void configure(const AutofocusParams& params);
    void reset();
    AutofocusResult analyzeFrame(const Eigen::MatrixXf& image);
    const std::vector<AutofocusResult>& results() const { return results_; }
    bool saveJson(const std::string& path) const;

private:
    AutofocusParams params_{};
    int frameIndex_ = 0;
    double bestMetric_ = 0.0;
    int bestFrameIndex_ = -1;
    std::vector<AutofocusResult> results_;
};

}  // namespace backproj
