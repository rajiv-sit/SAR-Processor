#pragma once

#include <cstdint>
#include <vector>

namespace rto {

struct RtoFrame {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint64_t timestampNs = 0;
    std::vector<float> pixels;
};

}  // namespace rto
