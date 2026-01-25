#include "app/PipelineRunner.hpp"

#include <chrono>
#include <cmath>
#include <thread>

#include "backproj/BackProjConfigLoader.hpp"
#include "backproj/BackProjectionEngine.hpp"
#include "pta/PtaAnalyzer.hpp"
#include "rpf/RpfProductStream.hpp"
#include "rto/RtoDataBus.hpp"

namespace app {

bool PipelineRunner::run() {
    backproj::BackProjConfigLoader loader;
    auto operatorConfig = loader.loadOperatorConfig("configs/backproj/operator.json");
    auto secondaryConfig = loader.loadSecondaryConfig("configs/backproj/secondary.json");

    backproj::BackProjectionEngine backprojEngine(operatorConfig, secondaryConfig);
    backprojEngine.run();

    rpf::RpfProductStream stream("data/rpf/sample.rpf");
    rpf::AnnotationStruct annotation{};
    rpf::LatLongGrid grid{};
    stream.nextBlock(annotation, grid, true);

    pta::PtaAnalyzer analyzer;
    pta::PtaChip chip{};
    analyzer.analyze1D(chip);

    rto::RtoDataBus bus("ipc://rto");
    rto::RtoFrame frame{};
    bus.publish(frame);

    return true;
}

bool PipelineRunner::runRtoPreview(const std::string& endpoint,
                                   std::uint32_t width,
                                   std::uint32_t height,
                                   std::size_t frames,
                                   std::uint32_t intervalMs) {
    backproj::BackProjConfigLoader loader;
    auto operatorConfig = loader.loadOperatorConfig("configs/backproj/operator.json");

    if (width == 0) {
        width = operatorConfig.nPixX > 0 ? operatorConfig.nPixX : 256;
    }
    if (height == 0) {
        height = operatorConfig.nPixY > 0 ? operatorConfig.nPixY : 256;
    }
    if (frames == 0) {
        frames = 1;
    }

    rto::RtoDataBus bus(endpoint);
    rto::RtoFrame frame{};
    frame.width = width;
    frame.height = height;
    frame.pixels.resize(static_cast<std::size_t>(width) * height);

    for (std::size_t frameIndex = 0; frameIndex < frames; ++frameIndex) {
        const float phase = static_cast<float>(frameIndex) * 0.15f;
        for (std::uint32_t y = 0; y < height; ++y) {
            const float fy = static_cast<float>(y) / static_cast<float>(height);
            for (std::uint32_t x = 0; x < width; ++x) {
                const float fx = static_cast<float>(x) / static_cast<float>(width);
                const float value =
                    0.5f + 0.5f * std::sin((fx * 6.2831853f) + phase) * std::cos((fy * 6.2831853f) - phase);
                frame.pixels[static_cast<std::size_t>(y) * width + x] = value;
            }
        }

        frame.timestampNs = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::system_clock::now().time_since_epoch())
                .count());

        if (!bus.publish(frame)) {
            return false;
        }

        if (frameIndex + 1 < frames && intervalMs > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(intervalMs));
        }
    }

    return true;
}

}  // namespace app
