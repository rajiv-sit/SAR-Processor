#include "sartape2/RFChainModel.hpp"

namespace sartape2 {

void RFChainModel::applyGain(ComplexBuffer& buffer, float gain) const {
    for (auto& sample : buffer) {
        sample *= gain;
    }
}

}  // namespace sartape2
