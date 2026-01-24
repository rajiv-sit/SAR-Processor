#include "rto/RtoDataBus.hpp"

namespace rto {

RtoDataBus::RtoDataBus(std::string endpoint)
    : endpoint_(std::move(endpoint)) {}

bool RtoDataBus::publish(const RtoFrame& /*frame*/) {
    return true;
}

}  // namespace rto
