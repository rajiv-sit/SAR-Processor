#pragma once

#include <string>
#include <vector>

#include <Eigen/Core>

namespace backproj {

struct AutofocusResult {
    int frameIndex = 0;
    double focusMetric = 0.0;
    bool isBest = false;
};

class AutofocusController {
public:
    void reset();
    AutofocusResult analyzeFrame(const Eigen::MatrixXf& image);
    const std::vector<AutofocusResult>& results() const { return results_; }
    bool saveJson(const std::string& path) const;

private:
    int frameIndex_ = 0;
    double bestMetric_ = 0.0;
    std::vector<AutofocusResult> results_;
};

}  // namespace backproj
