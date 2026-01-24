#pragma once

#include "backproj/AutofocusController.hpp"
#include "backproj/BackProjOperatorConfig.hpp"
#include "backproj/BackProjSecondaryConfig.hpp"
#include "backproj/RegistrationManager.hpp"

namespace backproj {

class BackProjectionEngine {
public:
    BackProjectionEngine(BackProjOperatorConfig operatorConfig,
                         BackProjSecondaryConfig secondaryConfig);

    void run();

private:
    BackProjOperatorConfig operatorConfig_;
    BackProjSecondaryConfig secondaryConfig_;
    RegistrationManager registrationManager_;
    AutofocusController autofocusController_;
};

}  // namespace backproj
