#include "backproj/BackProjectionEngine.hpp"

namespace backproj {

BackProjectionEngine::BackProjectionEngine(BackProjOperatorConfig operatorConfig,
                                           BackProjSecondaryConfig secondaryConfig)
    : operatorConfig_(std::move(operatorConfig)),
      secondaryConfig_(std::move(secondaryConfig)) {}

void BackProjectionEngine::run() {}

}  // namespace backproj
