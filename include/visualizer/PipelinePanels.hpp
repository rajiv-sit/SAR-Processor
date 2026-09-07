#pragma once

#include <array>
#include <string>

#include "visualizer/ScientificScan.hpp"

namespace sar::visualizer {

struct PipelinePanel {
    std::string id;
    std::string title;
    const ScientificFrame* frame = nullptr;
    std::string message;
    bool geographic = false;
};

// Four simultaneous views of one scan: range, azimuth, geocoding, final.
// Pointers borrow the scan's frames; keep the scan alive and unmodified while
// using the result. No pixel copies or placeholder images are created.
// Throws std::runtime_error if the scan's default stage is inconsistent.
std::array<PipelinePanel, 4> pipelinePanels(const ScientificScan& scan);

}  // namespace sar::visualizer
