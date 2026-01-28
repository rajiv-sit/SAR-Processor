#include "backproj/RegistrationManager.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <numeric>

#include <nlohmann/json.hpp>

namespace backproj {

namespace {

void applyShift(Eigen::MatrixXf& image, std::int32_t dx, std::int32_t dy) {
    if (dx == 0 && dy == 0) {
        return;
    }
    Eigen::MatrixXf shifted = Eigen::MatrixXf::Zero(image.rows(), image.cols());
    for (int row = 0; row < image.rows(); ++row) {
        const int srcRow = row - dy;
        if (srcRow < 0 || srcRow >= image.rows()) {
            continue;
        }
        for (int col = 0; col < image.cols(); ++col) {
            const int srcCol = col - dx;
            if (srcCol < 0 || srcCol >= image.cols()) {
                continue;
            }
            shifted(row, col) = image(srcRow, srcCol);
        }
    }
    image = std::move(shifted);
}

}  // namespace

void RegistrationManager::configure(const FrameRegistrationParams& params) {
    params_ = params;
}

void RegistrationManager::reset() {
    hasReference_ = false;
    refX_ = 0.0;
    refY_ = 0.0;
    frameIndex_ = 0;
    results_.clear();
}

RegistrationResult RegistrationManager::registerFrame(Eigen::MatrixXf& image, bool applyShiftFlag) {
    RegistrationResult result{};
    result.frameIndex = frameIndex_++;
    if (!results_.empty()) {
        result.cumulativeDx = results_.back().cumulativeDx;
        result.cumulativeDy = results_.back().cumulativeDy;
    }

    const double total = image.cwiseAbs().sum();
    if (total <= 0.0) {
        result.referenceX = refX_;
        result.referenceY = refY_;
        results_.push_back(result);
        return result;
    }

    double weightedX = 0.0;
    double weightedY = 0.0;
    for (int row = 0; row < image.rows(); ++row) {
        for (int col = 0; col < image.cols(); ++col) {
            const double weight = std::abs(image(row, col));
            weightedX += static_cast<double>(col) * weight;
            weightedY += static_cast<double>(row) * weight;
        }
    }

    const double cx = weightedX / total;
    const double cy = weightedY / total;

    if (!hasReference_) {
        hasReference_ = true;
        refX_ = cx;
        refY_ = cy;
        result.referenceX = refX_;
        result.referenceY = refY_;
        results_.push_back(result);
        return result;
    }

    const double dx = cx - refX_;
    const double dy = cy - refY_;
    result.dx = static_cast<std::int32_t>(std::lround(-dx));
    result.dy = static_cast<std::int32_t>(std::lround(-dy));
    result.metric = std::hypot(dx, dy);
    result.cumulativeDx += result.dx;
    result.cumulativeDy += result.dy;
    result.referenceX = refX_;
    result.referenceY = refY_;

    if (applyShiftFlag && params_.preShiftImageGrid) {
        applyShift(image, result.dx, result.dy);
    }

    if (params_.alphaAccum > 0.0 && params_.alphaAccum < 1.0) {
        refX_ = params_.alphaAccum * refX_ + (1.0 - params_.alphaAccum) * cx;
        refY_ = params_.alphaAccum * refY_ + (1.0 - params_.alphaAccum) * cy;
    }

    results_.push_back(result);
    return result;
}

bool RegistrationManager::saveJson(const std::string& path) const {
    nlohmann::json payload;
    payload["reference"] = {{"x", refX_}, {"y", refY_}};
    payload["params"] = {
        {"alphaAccum", params_.alphaAccum},
        {"preShiftImageGrid", params_.preShiftImageGrid},
        {"regisSearchSize", params_.regisSearchSize},
        {"marginBlanking", params_.marginBlanking},
        {"chipSize", params_.chipSize},
        {"magFactor", params_.magFactor},
        {"accumPow", params_.accumPow},
        {"detPow", params_.detPow}
    };
    payload["results"] = nlohmann::json::array();
    for (const auto& result : results_) {
        payload["results"].push_back({
            {"frameIndex", result.frameIndex},
            {"dx", result.dx},
            {"dy", result.dy},
            {"cumulativeDx", result.cumulativeDx},
            {"cumulativeDy", result.cumulativeDy},
            {"referenceX", result.referenceX},
            {"referenceY", result.referenceY},
            {"metric", result.metric}
        });
    }

    std::ofstream output(path);
    if (!output) {
        return false;
    }
    output << payload.dump(2) << '\n';
    return true;
}

}  // namespace backproj
