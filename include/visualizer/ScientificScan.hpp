#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "visualizer/ScientificFrame.hpp"

namespace sar::visualizer {

enum class ScientificStageStatus { Available, Unavailable };

struct ScientificScanStage {
    std::string id;
    std::string label;
    ScientificStageStatus status = ScientificStageStatus::Unavailable;
    std::string reason;
    std::filesystem::path manifestPath;
    std::optional<ScientificFrame> frame;
};

struct ScientificScan {
    std::string label;
    std::string source;
    std::string defaultStage;
    std::size_t defaultStageIndex = 0;
    std::vector<ScientificScanStage> stages;
    std::uint64_t totalPixels = 0;
};

// Load sar-scientific-scan-v1 plus its available sibling frame manifests.
// At most 16 uniquely identified stages, with at most 64 million total pixels.
// Unavailable stages carry a reason and no frame; the default must be available.
// Throws std::runtime_error with the scan filename for invalid metadata or I/O.
ScientificScan loadScientificScan(const std::filesystem::path& manifestPath,
                                  std::uint64_t pixelBudget = 64'000'000);

}  // namespace sar::visualizer
