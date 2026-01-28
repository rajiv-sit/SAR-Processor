#include "backproj/AutofocusController.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <vector>

#include <nlohmann/json.hpp>

namespace backproj {

namespace {

std::vector<AutofocusPoint> selectTopPoints(const Eigen::MatrixXf& image, std::size_t maxPoints) {
    std::vector<AutofocusPoint> points;
    if (image.size() == 0 || maxPoints == 0) {
        return points;
    }
    points.reserve(maxPoints);

    for (int row = 0; row < image.rows(); ++row) {
        for (int col = 0; col < image.cols(); ++col) {
            const float value = image(row, col);
            if (points.size() < maxPoints) {
                points.push_back({row, col, value});
                if (points.size() == maxPoints) {
                    std::sort(points.begin(), points.end(),
                              [](const AutofocusPoint& a, const AutofocusPoint& b) {
                                  return a.value > b.value;
                              });
                }
            } else if (value > points.back().value) {
                points.back() = {row, col, value};
                std::sort(points.begin(), points.end(),
                          [](const AutofocusPoint& a, const AutofocusPoint& b) {
                              return a.value > b.value;
                          });
            }
        }
    }

    return points;
}

}  // namespace

void AutofocusController::configure(const AutofocusParams& params) {
    params_ = params;
}

void AutofocusController::reset() {
    frameIndex_ = 0;
    bestMetric_ = 0.0;
    bestFrameIndex_ = -1;
    results_.clear();
}

AutofocusResult AutofocusController::analyzeFrame(const Eigen::MatrixXf& image) {
    AutofocusResult result{};
    result.frameIndex = frameIndex_++;
    result.selectionMethod = params_.selectionMethod;

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
    result.metricDelta = metric - bestMetric_;
    if (metric >= bestMetric_) {
        bestMetric_ = metric;
        result.isBest = true;
        bestFrameIndex_ = result.frameIndex;
    }
    if (params_.minMetricDelta > 0.0 && result.metricDelta < params_.minMetricDelta) {
        result.converged = true;
    }
    if (params_.maxFrames > 0 && static_cast<std::size_t>(result.frameIndex + 1) >= params_.maxFrames) {
        result.converged = true;
    }

    result.selectedPoints = selectTopPoints(image, params_.maxPoints);

    results_.push_back(result);
    return result;
}

bool AutofocusController::saveJson(const std::string& path) const {
    nlohmann::json payload;
    payload["bestMetric"] = bestMetric_;
    payload["bestFrameIndex"] = bestFrameIndex_;
    payload["params"] = {
        {"selectionMethod", params_.selectionMethod},
        {"minMetricDelta", params_.minMetricDelta},
        {"maxPoints", params_.maxPoints},
        {"maxFrames", params_.maxFrames}
    };
    payload["results"] = nlohmann::json::array();
    for (const auto& result : results_) {
        nlohmann::json entry = {
            {"frameIndex", result.frameIndex},
            {"focusMetric", result.focusMetric},
            {"metricDelta", result.metricDelta},
            {"isBest", result.isBest},
            {"converged", result.converged},
            {"selectionMethod", result.selectionMethod}
        };
        entry["selectedPoints"] = nlohmann::json::array();
        for (const auto& point : result.selectedPoints) {
            entry["selectedPoints"].push_back({
                {"row", point.row},
                {"col", point.col},
                {"value", point.value}
            });
        }
        payload["results"].push_back(std::move(entry));
    }

    std::ofstream output(path);
    if (!output) {
        return false;
    }
    output << payload.dump(2) << '\n';
    return true;
}

}  // namespace backproj
