#pragma once

#include "sartape2/Types.hpp"

namespace sartape2 {

class RFChainModel {
public:
    void applyGain(ComplexBuffer& buffer, float gain) const;
};

}  // namespace sartape2
