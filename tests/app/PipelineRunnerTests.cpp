#include <gtest/gtest.h>

#include "app/PipelineRunner.hpp"

TEST(PipelineRunnerTests, RtoPreviewFailsWithInvalidEndpoint) {
    app::PipelineRunner runner;
    EXPECT_FALSE(runner.runRtoPreview("udp://", 8, 8, 1, 0));
}

TEST(PipelineRunnerTests, RtoPreviewPublishesFrameWithDefaults) {
    app::PipelineRunner runner;
    EXPECT_TRUE(runner.runRtoPreview("udp://127.0.0.1:5005", 0, 0, 0, 0));
}
