#include "backproj/AutofocusController.hpp"

#include <cmath>
#include <fstream>

#include <nlohmann/json.hpp>

namespace backproj {

void AutofocusController::reset() {
    frameIndex_ = 0;
    bestMetric_ = 0.0;
    results_.clear();
}

AutofocusResult AutofocusController::analyzeFrame(const Eigen::MatrixXf& image) {
    AutofocusResult result{};
    result.frameIndex = frameIndex_++;

    if (image.size() == 0) {
        results_.push_back(result);
        return result;
    }

    double metric = 0.0;
    for (int row = 1; row < image.rows(); ++row) {
        for (int col = 1; col < image.cols(); ++col) {
            const float dx = image(row, col) - image(row, col - 1);
            const float dy = image(row, col) - image(row - 1, col);
            metric += std::abs(dx) + std::abs(dy);
        }
    }

    result.focusMetric = metric;
    if (metric >= bestMetric_) {
        bestMetric_ = metric;
        result.isBest = true;
    }

    results_.push_back(result);
    return result;
}

bool AutofocusController::saveJson(const std::string& path) const {
    nlohmann::json payload;
    payload["results"] = nlohmann::json::array();
    for (const auto& result : results_) {
        payload["results"].push_back({
            {"frameIndex", result.frameIndex},
            {"focusMetric", result.focusMetric},
            {"isBest", result.isBest}
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
