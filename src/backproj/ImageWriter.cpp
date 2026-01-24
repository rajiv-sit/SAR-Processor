#include "backproj/ImageWriter.hpp"

#include <fstream>

namespace backproj {

bool writeTiffStub(const std::string& path, const Eigen::MatrixXf& image) {
    std::ofstream output(path);
    if (!output) {
        return false;
    }
    output << "TIFF_STUB\n";
    output << "width=" << image.cols() << "\n";
    output << "height=" << image.rows() << "\n";
    for (int row = 0; row < image.rows(); ++row) {
        for (int col = 0; col < image.cols(); ++col) {
            output << image(row, col);
            if (col + 1 < image.cols()) {
                output << ' ';
            }
        }
        output << "\n";
    }
    return true;
}

}  // namespace backproj
