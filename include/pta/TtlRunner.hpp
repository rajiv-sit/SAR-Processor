#pragma once

#include <string>

namespace pta {

class TtlRunner {
public:
    bool runFromConfig(const std::string& path);
};

}  // namespace pta
