#pragma once

#include <complex>
#include <string>
#include <vector>

#include <Eigen/Dense>
#include <unsupported/Eigen/FFT>

#include "backproj/AutofocusController.hpp"
#include "backproj/BackProjOperatorConfig.hpp"
#include "backproj/BackProjSecondaryConfig.hpp"
#include "backproj/IBackProjectionEngine.hpp"
#include "backproj/RegistrationManager.hpp"
#include "rpf/LatLongGrid.hpp"

namespace backproj {

class BackProjectionEngine : public IBackProjectionEngine {
public:
    BackProjectionEngine(BackProjOperatorConfig operatorConfig,
                         BackProjSecondaryConfig secondaryConfig);

    void run() override;
    Eigen::MatrixXf generateImage() override;
    Eigen::MatrixXf runWithOutputs();

private:
    BackProjOperatorConfig operatorConfig_;
    BackProjSecondaryConfig secondaryConfig_;
    RegistrationManager registrationManager_;
    AutofocusController autofocusController_;
    rpf::LatLongGrid lastLatLongGrid_{};
    bool hasLatLongGrid_ = false;
    std::string lastSourcePath_{};
    Eigen::FFT<float> rowFft_;
    Eigen::FFT<float> colFft_;
    std::vector<std::complex<float>> fftRowInput_;
    std::vector<std::complex<float>> fftRowOutput_;
    std::vector<std::complex<float>> fftColInput_;
    std::vector<std::complex<float>> fftColOutput_;
    void fftRows(Eigen::MatrixXcf& data);
    void fftCols(Eigen::MatrixXcf& data);
};

}  // namespace backproj
