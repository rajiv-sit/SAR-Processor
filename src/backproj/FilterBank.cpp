#include "backproj/FilterBank.hpp"

#include <cmath>

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
        const double phase = (denom > 0.0) ? (2.0 * M_PI * static_cast<double>(i) / denom) : 0.0;
        window(static_cast<int>(i)) = static_cast<float>(alpha - beta * std::cos(phase));
    }
    return window;
}

}  // namespace

FilterBank::FilterBank(FilterParams rangeParams, FilterParams azimuthParams)
    : rangeParams_(std::move(rangeParams)),
      azimuthParams_(std::move(azimuthParams)) {}

Eigen::VectorXf FilterBank::rangeWindow(std::size_t size) const {
    return hammingWindow(size, rangeParams_.windowCoef);
}

Eigen::VectorXf FilterBank::azimuthWindow(std::size_t size) const {
    return hammingWindow(size, azimuthParams_.windowCoef);
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

}  // namespace backproj
