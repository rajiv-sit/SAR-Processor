#pragma once

#include <vector>

#include "sartape2/Types.hpp"

namespace sartape2 {

struct Target {
    Vec3 position{};
    Vec3 velocity{};
    double rcs = 1.0;
};

struct TargetState {
    Vec3 position{};
    double rcs = 1.0;
};

class SceneModel {
public:
    void addTarget(const Target& target);
    std::vector<TargetState> targetsAt(double timeSec) const;

private:
    std::vector<Target> targets_;
};

}  // namespace sartape2
