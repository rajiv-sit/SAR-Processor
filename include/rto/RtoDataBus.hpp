#pragma once

#include <cstdint>
#include <string>

#include "rto/RtoFrame.hpp"

namespace rto {

class RtoDataBus {
public:
    explicit RtoDataBus(std::string endpoint);

    bool publish(const RtoFrame& frame);

private:
    std::string endpoint_;
    std::string host_;
    std::uint16_t port_ = 0;
};

}  // namespace rto
