#include "backproj/BackProjectionEngine.hpp"

#include <string>

#include "backproj/FilterBank.hpp"
#include "backproj/ImageWriter.hpp"

namespace backproj {

BackProjectionEngine::BackProjectionEngine(BackProjOperatorConfig operatorConfig,
                                           BackProjSecondaryConfig secondaryConfig)
    : operatorConfig_(std::move(operatorConfig)),
      secondaryConfig_(std::move(secondaryConfig)) {}

Eigen::MatrixXf BackProjectionEngine::generateImage() {
    if (operatorConfig_.nPixX == 0 || operatorConfig_.nPixY == 0) {
        return {};
    }

    Eigen::MatrixXf image(static_cast<int>(operatorConfig_.nPixY),
                          static_cast<int>(operatorConfig_.nPixX));
    for (int row = 0; row < image.rows(); ++row) {
        for (int col = 0; col < image.cols(); ++col) {
            image(row, col) = static_cast<float>(row + col);
        }
    }

    FilterBank filters(secondaryConfig_.rngFilterParams,
                       secondaryConfig_.azmFilterParams);
    const auto rangeWindow = filters.rangeWindow(operatorConfig_.nPixX);
    const auto azWindow = filters.azimuthWindow(operatorConfig_.nPixY);
    FilterBank::applyWindow(image, rangeWindow, true);
    FilterBank::applyWindow(image, azWindow, false);

    registrationManager_.configure(secondaryConfig_.frameRegistrationParams);
    if (operatorConfig_.applyFrameRegistration) {
        registrationManager_.registerFrame(image, true);
    }

    if (operatorConfig_.applyAutoFocus) {
        autofocusController_.analyzeFrame(image);
    }

    return image;
}

void BackProjectionEngine::run() {
    Eigen::MatrixXf image = generateImage();
    if (image.size() == 0) {
        return;
    }

    std::string outputPath = "backproj_stub.tif";
    if (!operatorConfig_.rpfBaseFileName.empty()) {
        outputPath = operatorConfig_.rpfBaseFileName + "_stub.tif";
    }
    writeTiff(outputPath, image);

    const std::string basePath =
        operatorConfig_.rpfBaseFileName.empty() ? "backproj" : operatorConfig_.rpfBaseFileName;
    if (!registrationManager_.results().empty()) {
        registrationManager_.saveJson(basePath + "_registration.json");
    }
    if (!autofocusController_.results().empty()) {
        autofocusController_.saveJson(basePath + "_autofocus.json");
    }
}

}  // namespace backproj
