#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <Eigen/Core>

#include "backproj/BackProjSecondaryConfig.hpp"

namespace backproj {

struct RegistrationResult {
    std::int32_t frameIndex = 0;
    std::int32_t dx = 0;
    std::int32_t dy = 0;
    std::int32_t cumulativeDx = 0;
    std::int32_t cumulativeDy = 0;
    double referenceX = 0.0;
    double referenceY = 0.0;
    double metric = 0.0;
};

class RegistrationManager {
public:
    void configure(const FrameRegistrationParams& params);
    void reset();
    RegistrationResult registerFrame(Eigen::MatrixXf& image, bool applyShiftFlag);
    const std::vector<RegistrationResult>& results() const { return results_; }
    bool saveJson(const std::string& path) const;

private:
    FrameRegistrationParams params_{};
    bool hasReference_ = false;
    double refX_ = 0.0;
    double refY_ = 0.0;
    std::int32_t frameIndex_ = 0;
    std::vector<RegistrationResult> results_;
};

}  // namespace backproj
