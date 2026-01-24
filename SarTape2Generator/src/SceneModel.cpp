#include "sartape2/SceneModel.hpp"

namespace sartape2 {

void SceneModel::addTarget(const Target& target) {
    targets_.push_back(target);
}

std::vector<TargetState> SceneModel::targetsAt(double timeSec) const {
    std::vector<TargetState> result;
    result.reserve(targets_.size());
    for (const auto& target : targets_) {
        TargetState state{};
        state.position.x = target.position.x + target.velocity.x * timeSec;
        state.position.y = target.position.y + target.velocity.y * timeSec;
        state.position.z = target.position.z + target.velocity.z * timeSec;
        state.rcs = target.rcs;
        result.push_back(state);
    }
    return result;
}

}  // namespace sartape2
