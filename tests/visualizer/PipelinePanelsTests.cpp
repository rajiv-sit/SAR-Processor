#include <gtest/gtest.h>

#include <array>
#include <stdexcept>
#include <string>
#include <utility>

#include "visualizer/PipelinePanels.hpp"

namespace {

using sar::visualizer::GeolocationGrid;
using sar::visualizer::pipelinePanels;
using sar::visualizer::ScientificFrame;
using sar::visualizer::ScientificScan;
using sar::visualizer::ScientificScanStage;
using sar::visualizer::ScientificStageStatus;

ScientificScanStage available(std::string id, float value, bool geographic = false) {
    ScientificScanStage stage;
    stage.id = std::move(id);
    stage.status = ScientificStageStatus::Available;
    stage.frame = ScientificFrame{};
    stage.frame->width = 1;
    stage.frame->height = 1;
    stage.frame->pixels = {value};
    if (geographic) {
        stage.frame->geolocation = GeolocationGrid{{0}, {0}, {-106}, {35}, 0};
    }
    return stage;
}

ScientificScanStage unavailable(std::string id, std::string reason) {
    ScientificScanStage stage;
    stage.id = std::move(id);
    stage.reason = std::move(reason);
    return stage;
}

TEST(PipelinePanelsTests, GotchaPanelsShowLocalRangeBackprojectionAndFinalWithGeocodingReason) {
    ScientificScan scan;
    scan.defaultStage = "backprojection";
    scan.defaultStageIndex = 1;
    scan.stages = {available("range", 10), available("backprojection", 20),
                   unavailable("geocoded", "No verified Earth origin for this aperture.")};
    const auto panels = pipelinePanels(scan);
    ASSERT_EQ(panels.size(), 4);
    const std::array<std::string, 4> ids{"range", "azimuth", "geocoding", "final"};
    const std::array<std::string, 4> titles{"Range compression",
                                            "Backprojection / azimuth compression",
                                            "Geocoordinates", "Final image"};
    for (std::size_t i = 0; i < panels.size(); ++i) {
        EXPECT_EQ(panels[i].id, ids[i]);
        EXPECT_EQ(panels[i].title, titles[i]);
        EXPECT_FALSE(panels[i].geographic);
    }
    EXPECT_EQ(panels[0].frame, &*scan.stages[0].frame);
    EXPECT_EQ(panels[1].frame, &*scan.stages[1].frame);
    EXPECT_EQ(panels[3].frame, panels[1].frame);
    EXPECT_EQ(panels[2].frame, nullptr);
    EXPECT_EQ(panels[2].message, scan.stages[2].reason);
    EXPECT_EQ(panels[3].message, "Scene-local image; Earth coordinates are unavailable.");
    EXPECT_TRUE(panels[0].message.empty());
    EXPECT_TRUE(panels[1].message.empty());
}

TEST(PipelinePanelsTests, SandiaPanelsIdentifyUpstreamFocusAndShareGeocodedFinal) {
    ScientificScan scan;
    scan.defaultStage = "geocoded";
    scan.defaultStageIndex = 3;
    scan.stages = {unavailable("range", "Range compression was performed upstream."),
                   unavailable("backprojection", "Input is already focused."),
                   available("focused_magnitude", 30), available("geocoded", 40, true)};
    const auto panels = pipelinePanels(scan);
    EXPECT_EQ(panels[0].frame, nullptr);
    EXPECT_EQ(panels[0].message, scan.stages[0].reason);
    EXPECT_EQ(panels[1].frame, &*scan.stages[2].frame);
    EXPECT_EQ(panels[1].message,
              "Producer-focused image; azimuth compression was performed upstream.");
    EXPECT_EQ(panels[2].frame, &*scan.stages[3].frame);
    EXPECT_EQ(panels[3].frame, panels[2].frame);
    EXPECT_TRUE(panels[2].geographic);
    EXPECT_TRUE(panels[3].geographic);
    EXPECT_TRUE(panels[2].message.empty());
    EXPECT_TRUE(panels[3].message.empty());
}

TEST(PipelinePanelsTests,
     AllStageAvailabilityCombinationsKeepIndependentPanelsAndPreferBackprojection) {
    for (unsigned mask = 0; mask < 16; ++mask) {
        SCOPED_TRACE(mask);
        ScientificScan scan;
        scan.defaultStage = "final_product";
        scan.defaultStageIndex = 4;
        const std::array<std::string, 4> ids{"range", "backprojection", "focused_magnitude",
                                             "geocoded"};
        for (unsigned i = 0; i < 4; ++i) {
            scan.stages.push_back(mask & (1U << i)
                                      ? available(ids[i], static_cast<float>(i), i == 3)
                                      : unavailable(ids[i], ids[i] + " unavailable"));
        }
        scan.stages.push_back(available("final_product", 99));
        const auto panels = pipelinePanels(scan);
        EXPECT_EQ(panels[0].frame, mask & 1U ? &*scan.stages[0].frame : nullptr);
        EXPECT_EQ(panels[1].frame, mask & 2U   ? &*scan.stages[1].frame
                                   : mask & 4U ? &*scan.stages[2].frame
                                               : nullptr);
        EXPECT_EQ(panels[2].frame, mask & 8U ? &*scan.stages[3].frame : nullptr);
        EXPECT_EQ(panels[3].frame, &*scan.stages[4].frame);
        EXPECT_EQ(panels[2].geographic, (mask & 8U) != 0);
        for (const auto& panel : panels) {
            if (!panel.frame) EXPECT_FALSE(panel.message.empty());
        }
    }
}

TEST(PipelinePanelsTests, MissingStagesHaveExplanationsWithoutInventingImages) {
    ScientificScan scan;
    scan.defaultStage = "final_product";
    scan.stages = {available("final_product", 7)};
    const auto panels = pipelinePanels(scan);
    for (std::size_t i = 0; i < 3; ++i) {
        EXPECT_EQ(panels[i].frame, nullptr);
        EXPECT_FALSE(panels[i].message.empty());
        EXPECT_FALSE(panels[i].geographic);
    }
    EXPECT_EQ(panels[3].frame, &*scan.stages[0].frame);
}

TEST(PipelinePanelsTests, UsesFocusedStageReasonWhenBackprojectionStageIsAbsent) {
    ScientificScan scan;
    scan.defaultStage = "final_product";
    scan.stages = {available("final_product", 7),
                   unavailable("focused_magnitude", "No focused complex input supplied.")};
    const auto panels = pipelinePanels(scan);
    EXPECT_EQ(panels[1].frame, nullptr);
    EXPECT_EQ(panels[1].message, scan.stages[1].reason);
}

TEST(PipelinePanelsTests, GeocodingNeedsCoordinatesEvenIfStageClaimsAvailability) {
    ScientificScan scan;
    scan.defaultStage = "geocoded";
    scan.stages = {available("geocoded", 7)};
    const auto panels = pipelinePanels(scan);
    EXPECT_EQ(panels[2].frame, nullptr);
    EXPECT_FALSE(panels[2].geographic);
    EXPECT_EQ(panels[2].message, "Geocoded stage has no geographic coordinates.");
    EXPECT_EQ(panels[3].frame, &*scan.stages[0].frame);
    EXPECT_FALSE(panels[3].geographic);
}

TEST(PipelinePanelsTests, EmptyReasonAndMissingAvailableFrameUseExplanations) {
    ScientificScan scan;
    scan.defaultStage = "final_product";
    scan.stages = {available("final_product", 7), unavailable("range", ""),
                   available("backprojection", 1)};
    scan.stages[2].frame.reset();
    const auto panels = pipelinePanels(scan);
    EXPECT_EQ(panels[0].frame, nullptr);
    EXPECT_FALSE(panels[0].message.empty());
    EXPECT_EQ(panels[1].frame, nullptr);
    EXPECT_FALSE(panels[1].message.empty());
}

TEST(PipelinePanelsTests, DoesNotExposePayloadOnUnavailableStage) {
    ScientificScan scan;
    scan.defaultStage = "final_product";
    scan.stages = {available("final_product", 7), available("range", 1),
                   available("backprojection", 2), available("geocoded", 3, true)};
    for (std::size_t i = 1; i < scan.stages.size(); ++i) {
        scan.stages[i].status = ScientificStageStatus::Unavailable;
    }
    const auto panels = pipelinePanels(scan);
    for (std::size_t i = 0; i < 3; ++i) {
        EXPECT_EQ(panels[i].frame, nullptr);
        EXPECT_FALSE(panels[i].geographic);
    }
}

TEST(PipelinePanelsTests, PanelPointersStayWithinCurrentScanAndBorrowOriginalPixels) {
    ScientificScan first;
    first.defaultStage = "backprojection";
    first.stages = {available("backprojection", 10, true)};
    ScientificScan second;
    second.defaultStage = "backprojection";
    second.stages = {available("backprojection", 20, true)};
    const auto previous = pipelinePanels(first);
    const auto current = pipelinePanels(second);
    EXPECT_NE(current[1].frame, previous[1].frame);
    EXPECT_EQ(current[1].frame, &*second.stages[0].frame);
    EXPECT_EQ(current[1].frame->pixels.data(), second.stages[0].frame->pixels.data());
    EXPECT_EQ(current[3].frame, current[1].frame);
    EXPECT_EQ(current[1].frame->pixels[0], 20);
    EXPECT_EQ(previous[1].frame->pixels[0], 10);
    EXPECT_TRUE(current[1].geographic);
}

TEST(PipelinePanelsTests, RejectsInvalidDefaultWithDescriptiveErrors) {
    ScientificScan scan;
    try {
        pipelinePanels(scan);
        FAIL() << "Expected missing default rejection";
    } catch (const std::runtime_error& error) {
        EXPECT_NE(std::string(error.what()).find("default stage index"), std::string::npos);
    }
    scan.defaultStage = "backprojection";
    scan.stages = {available("backprojection", 1)};
    scan.defaultStageIndex = 1;
    EXPECT_THROW(pipelinePanels(scan), std::runtime_error);
    scan.defaultStageIndex = 0;
    scan.defaultStage = "wrong_id";
    EXPECT_THROW(pipelinePanels(scan), std::runtime_error);
    scan.defaultStage = "backprojection";
    scan.stages[0].status = ScientificStageStatus::Unavailable;
    EXPECT_THROW(pipelinePanels(scan), std::runtime_error);
    scan.stages[0].status = ScientificStageStatus::Available;
    scan.stages[0].frame.reset();
    EXPECT_THROW(pipelinePanels(scan), std::runtime_error);
}

}  // namespace
