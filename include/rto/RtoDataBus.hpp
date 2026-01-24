#pragma once

#include <string>

#include "rto/RtoFrame.hpp"

namespace rto {

class RtoDataBus {
public:
    explicit RtoDataBus(std::string endpoint);

    bool publish(const RtoFrame& frame);

private:
    std::string endpoint_;
};

}  // namespace rto
