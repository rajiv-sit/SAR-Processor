#include "visualizer/PipelinePanels.hpp"

#include <stdexcept>
#include <string_view>

namespace sar::visualizer {
namespace {

const ScientificScanStage* findStage(const ScientificScan& scan, std::string_view id) {
    for (const auto& stage : scan.stages) {
        if (stage.id == id) {
            return &stage;
        }
    }
    return nullptr;
}

const ScientificFrame* availableFrame(const ScientificScanStage* stage) {
    return stage && stage->status == ScientificStageStatus::Available && stage->frame
               ? &*stage->frame
               : nullptr;
}

std::string unavailableMessage(const ScientificScanStage* stage, const char* fallback) {
    return stage && !stage->reason.empty() ? stage->reason : fallback;
}

}  // namespace

std::array<PipelinePanel, 4> pipelinePanels(const ScientificScan& scan) {
    if (scan.defaultStageIndex >= scan.stages.size()) {
        throw std::runtime_error("Scientific scan default stage index is out of range");
    }
    const auto& defaultStage = scan.stages[scan.defaultStageIndex];
    if (defaultStage.id != scan.defaultStage) {
        throw std::runtime_error("Scientific scan default stage ID does not match its index");
    }
    const auto* finalFrame = availableFrame(&defaultStage);
    if (!finalFrame) {
        throw std::runtime_error("Scientific scan default stage requires an available frame");
    }

    std::array<PipelinePanel, 4> panels{{{"range", "Range compression"},
                                         {"azimuth", "Backprojection / azimuth compression"},
                                         {"geocoding", "Geocoordinates"},
                                         {"final", "Final image"}}};
    const auto* range = findStage(scan, "range");
    panels[0].frame = availableFrame(range);
    if (!panels[0].frame) {
        panels[0].message = unavailableMessage(range, "Range-compressed data is unavailable.");
    }

    const auto* backprojection = findStage(scan, "backprojection");
    panels[1].frame = availableFrame(backprojection);
    if (!panels[1].frame) {
        const auto* focused = findStage(scan, "focused_magnitude");
        panels[1].frame = availableFrame(focused);
        if (panels[1].frame) {
            panels[1].message =
                "Producer-focused image; azimuth compression was performed upstream.";
        } else {
            panels[1].message =
                unavailableMessage(backprojection ? backprojection : focused,
                                   "Backprojection and focused azimuth data are unavailable.");
        }
    }

    const auto* geocoded = findStage(scan, "geocoded");
    panels[2].frame = availableFrame(geocoded);
    if (panels[2].frame && !panels[2].frame->geolocation) {
        panels[2].frame = nullptr;
        panels[2].message = "Geocoded stage has no geographic coordinates.";
    } else if (!panels[2].frame) {
        panels[2].message = unavailableMessage(geocoded, "Geographic coordinates are unavailable.");
    }

    panels[3].frame = finalFrame;
    if (!finalFrame->geolocation) {
        panels[3].message = "Scene-local image; Earth coordinates are unavailable.";
    }
    for (auto& panel : panels) {
        panel.geographic = panel.frame && panel.frame->geolocation.has_value();
    }
    return panels;
}

}  // namespace sar::visualizer
