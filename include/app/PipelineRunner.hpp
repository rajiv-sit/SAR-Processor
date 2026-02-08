#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace app {

class PipelineRunner {
public:
    bool run();
    bool runRtoPreview(const std::string& endpoint,
                       const std::optional<std::filesystem::path>& sharedFile,
                       const std::optional<std::filesystem::path>& cachedRaw,
                       std::uint32_t width,
                       std::uint32_t height,
                       std::size_t frames,
                       std::uint32_t intervalMs);
};

}  // namespace app
