#pragma once

#include <string>

#include "backproj/BackProjOperatorConfig.hpp"
#include "backproj/BackProjSecondaryConfig.hpp"

namespace backproj {

class BackProjConfigLoader {
public:
    BackProjOperatorConfig loadOperatorConfig(const std::string& path) const;
    BackProjSecondaryConfig loadSecondaryConfig(const std::string& path) const;
};

}  // namespace backproj
