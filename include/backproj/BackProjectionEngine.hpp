#pragma once

#include <string>

#include <Eigen/Dense>

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
};

}  // namespace backproj
