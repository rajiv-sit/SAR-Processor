#include "backproj/FilterBank.hpp"

#include <cmath>
#include <numbers>
#include <string>

namespace backproj {

namespace {

Eigen::VectorXf hammingWindow(std::size_t size, double coef) {
    Eigen::VectorXf window(static_cast<int>(size));
    if (size == 0) {
        return window;
    }
    const double alpha = (coef <= 0.0) ? 0.54 : coef;
    const double beta = 1.0 - alpha;
    const double denom = static_cast<double>(size - 1);
    for (std::size_t i = 0; i < size; ++i) {
        const double phase =
            (denom > 0.0) ? (2.0 * std::numbers::pi_v<double> * static_cast<double>(i) / denom) : 0.0;
        window(static_cast<int>(i)) = static_cast<float>(alpha - beta * std::cos(phase));
    }
    return window;
}

void applyBroadening(Eigen::VectorXf& window, double broadening) {
    if (window.size() == 0 || broadening <= 0.0 || broadening == 1.0) {
        return;
    }
    const double exponent = 1.0 / broadening;
    for (int i = 0; i < window.size(); ++i) {
        window(i) = static_cast<float>(std::pow(window(i), exponent));
    }
}

void applyScaling(Eigen::VectorXf& window, const std::string& method) {
    if (window.size() == 0 || method.empty()) {
        return;
    }
    if (method == "totalpower") {
        const double power = window.array().square().mean();
        if (power > 0.0) {
            window /= static_cast<float>(std::sqrt(power));
        }
    } else if (method == "sum") {
        const float sum = window.sum();
        if (sum != 0.0f) {
            window /= sum;
        }
    }
}

}  // namespace

FilterBank::FilterBank(FilterParams rangeParams, FilterParams azimuthParams)
    : rangeParams_(std::move(rangeParams)),
      azimuthParams_(std::move(azimuthParams)) {}

Eigen::VectorXf FilterBank::rangeWindow(std::size_t size) const {
    Eigen::VectorXf window = hammingWindow(size, rangeParams_.windowCoef);
    applyBroadening(window, rangeParams_.windowBroadening);
    applyScaling(window, rangeParams_.scalingMethod);
    return window;
}

Eigen::VectorXf FilterBank::azimuthWindow(std::size_t size) const {
    Eigen::VectorXf window = hammingWindow(size, azimuthParams_.windowCoef);
    applyBroadening(window, azimuthParams_.windowBroadening);
    applyScaling(window, azimuthParams_.scalingMethod);
    return window;
}

void FilterBank::applyWindow(Eigen::MatrixXf& image,
                             const Eigen::VectorXf& window,
                             bool alongColumns) {
    if (image.size() == 0 || window.size() == 0) {
        return;
    }
    if (alongColumns && image.cols() == window.size()) {
        for (int row = 0; row < image.rows(); ++row) {
            image.row(row) = image.row(row).cwiseProduct(window.transpose());
        }
    } else if (!alongColumns && image.rows() == window.size()) {
        for (int col = 0; col < image.cols(); ++col) {
            image.col(col) = image.col(col).cwiseProduct(window);
        }
    }
}

void FilterBank::applyWindow(Eigen::MatrixXcf& image,
                             const Eigen::VectorXf& window,
                             bool alongColumns) {
    if (image.size() == 0 || window.size() == 0) {
        return;
    }
    if (alongColumns && image.cols() == window.size()) {
        for (int row = 0; row < image.rows(); ++row) {
            for (int col = 0; col < image.cols(); ++col) {
                image(row, col) *= window(col);
            }
        }
    } else if (!alongColumns && image.rows() == window.size()) {
        for (int col = 0; col < image.cols(); ++col) {
            for (int row = 0; row < image.rows(); ++row) {
                image(row, col) *= window(row);
            }
        }
    }
}

}  // namespace backproj
