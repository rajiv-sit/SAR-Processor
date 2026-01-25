#include "app/PipelineRunner.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <thread>

#include <Eigen/Dense>

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
    auto secondaryConfig = loader.loadSecondaryConfig("configs/backproj/secondary.json");

    if (width == 0) {
        width = operatorConfig.nPixX > 0 ? operatorConfig.nPixX : 256;
    }
    if (height == 0) {
        height = operatorConfig.nPixY > 0 ? operatorConfig.nPixY : 256;
    }
    if (frames == 0) {
        frames = 1;
    }

    operatorConfig.nPixX = width;
    operatorConfig.nPixY = height;

    backproj::BackProjectionEngine engine(operatorConfig, secondaryConfig);
    const Eigen::MatrixXf image = engine.generateImage();
    if (image.size() == 0) {
        return false;
    }

    rto::RtoDataBus bus(endpoint);
    rto::RtoFrame frame{};
    std::uint32_t outWidth = static_cast<std::uint32_t>(image.cols());
    std::uint32_t outHeight = static_cast<std::uint32_t>(image.rows());

    constexpr std::size_t kUdpMaxPayload = 65507;
    const std::size_t maxPixels = (kUdpMaxPayload - 16) / sizeof(float);
    const std::size_t pixelCount = static_cast<std::size_t>(outWidth) * outHeight;
    if (pixelCount > maxPixels) {
        const double scale = std::sqrt(static_cast<double>(maxPixels) / static_cast<double>(pixelCount));
        outWidth = std::max<std::uint32_t>(1u, static_cast<std::uint32_t>(outWidth * scale));
        outHeight = std::max<std::uint32_t>(1u, static_cast<std::uint32_t>(outHeight * scale));
        while (static_cast<std::size_t>(outWidth) * outHeight > maxPixels) {
            if (outWidth >= outHeight && outWidth > 1) {
                --outWidth;
            } else if (outHeight > 1) {
                --outHeight;
            } else {
                break;
            }
        }
    }

    frame.width = outWidth;
    frame.height = outHeight;
    frame.pixels.resize(static_cast<std::size_t>(frame.width) * frame.height);
    if (outWidth == static_cast<std::uint32_t>(image.cols()) &&
        outHeight == static_cast<std::uint32_t>(image.rows())) {
        for (int row = 0; row < image.rows(); ++row) {
            for (int col = 0; col < image.cols(); ++col) {
                frame.pixels[static_cast<std::size_t>(row) * frame.width + col] = image(row, col);
            }
        }
    } else {
        for (std::uint32_t y = 0; y < outHeight; ++y) {
            const int srcY = static_cast<int>(
                static_cast<std::size_t>(y) * image.rows() / outHeight);
            for (std::uint32_t x = 0; x < outWidth; ++x) {
                const int srcX = static_cast<int>(
                    static_cast<std::size_t>(x) * image.cols() / outWidth);
                frame.pixels[static_cast<std::size_t>(y) * outWidth + x] = image(srcY, srcX);
            }
        }
    }

    for (std::size_t frameIndex = 0; frameIndex < frames; ++frameIndex) {
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
