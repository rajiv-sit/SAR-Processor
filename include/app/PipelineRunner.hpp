#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace app {

class PipelineRunner {
public:
    bool run();
    bool runRtoPreview(const std::string& endpoint,
                       std::uint32_t width,
                       std::uint32_t height,
                       std::size_t frames,
                       std::uint32_t intervalMs);
};

}  // namespace app
