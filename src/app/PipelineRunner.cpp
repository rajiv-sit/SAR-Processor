#include "app/PipelineRunner.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <thread>

#include <Eigen/Dense>
#include <nlohmann/json.hpp>

#include "backproj/BackProjConfigLoader.hpp"
#include "backproj/BackProjectionEngine.hpp"
#include "pta/PtaAnalyzer.hpp"
#include "rto/RtoDataBus.hpp"
#include "sar/SarTapeIngestPipeline.hpp"
#include "sar/SarTapeToRpf.hpp"

namespace app {

namespace {

std::string readEnv(const char* name) {
    const char* value = std::getenv(name);
    return value ? std::string(value) : std::string();
}

std::uint32_t parseEnvU32(const char* name, std::uint32_t fallback) {
    const std::string value = readEnv(name);
    if (value.empty()) {
        return fallback;
    }
    try {
        const auto parsed = static_cast<std::uint32_t>(std::stoul(value));
        return parsed;
    } catch (...) {
        return fallback;
    }
}

pta::PtaChip buildCenterChip(const Eigen::MatrixXf& image, int rows, int cols) {
    pta::PtaChip chip{};
    if (image.size() == 0) {
        return chip;
    }
    const int imgRows = static_cast<int>(image.rows());
    const int imgCols = static_cast<int>(image.cols());
    const int useRows = std::max<int>(1, std::min<int>(rows, imgRows));
    const int useCols = std::max<int>(1, std::min<int>(cols, imgCols));
    const int startRow = std::max<int>(0, (imgRows - useRows) / 2);
    const int startCol = std::max<int>(0, (imgCols - useCols) / 2);
    chip.chipIn = image.block(startRow, startCol, useRows, useCols);
    return chip;
}

}  // namespace

bool PipelineRunner::run() {
    backproj::BackProjConfigLoader loader;
    auto operatorConfig = loader.loadOperatorConfig("configs/backproj/operator.json");
    auto secondaryConfig = loader.loadSecondaryConfig("configs/backproj/secondary.json");

    const std::filesystem::path outputDir =
        readEnv("SAR_PIPELINE_OUTPUT_DIR").empty()
            ? std::filesystem::path("output")
            : std::filesystem::path(readEnv("SAR_PIPELINE_OUTPUT_DIR"));
    std::filesystem::create_directories(outputDir);

    std::string outputPrefix = readEnv("SAR_PIPELINE_OUTPUT_PREFIX");
    if (outputPrefix.empty()) {
        outputPrefix = (outputDir / "sartape_output").string();
    }

    const std::string sarTapePath = readEnv("SAR_PIPELINE_SARTAPE");
    std::string rpfPath = readEnv("SAR_PIPELINE_RPF");

    if (!sarTapePath.empty()) {
        sar::IngestOptions options{};
        options.errorPolicy = sar::ErrorPolicy::kFatal;
        options.outputComplexIq = false;

        sar::SarTapeIngestPipeline pipeline(sarTapePath, outputPrefix, options);
        const auto lines = pipeline.run();
        if (lines == 0) {
            return false;
        }

        if (rpfPath.empty()) {
            rpfPath = outputPrefix + ".rpf";
        }

        const std::uint32_t maxLines = parseEnvU32("SAR_PIPELINE_MAX_LINES", 0);
        std::string error;
        if (!sar::writeRpfFromSarTape(sarTapePath, rpfPath, maxLines, error)) {
            return false;
        }
    }

    if (rpfPath.empty() && !operatorConfig.inputFileName.empty()) {
        std::filesystem::path inputPath = operatorConfig.inputFileName;
        if (!operatorConfig.inputFilePath.empty()) {
            inputPath = std::filesystem::path(operatorConfig.inputFilePath) / operatorConfig.inputFileName;
        }
        rpfPath = inputPath.string();
    }

    if (!rpfPath.empty()) {
        const std::filesystem::path inputPath = rpfPath;
        operatorConfig.inputFilePath = inputPath.has_parent_path()
                                           ? inputPath.parent_path().string()
                                           : std::string();
        operatorConfig.inputFileName = inputPath.filename().string();
    }

    operatorConfig.rpfBaseFileName = (outputDir / "pipeline").string();

    backproj::BackProjectionEngine backprojEngine(operatorConfig, secondaryConfig);
    const Eigen::MatrixXf image = backprojEngine.runWithOutputs();
    if (image.size() == 0) {
        return false;
    }

    pta::PtaAnalyzer analyzer;
    const std::uint32_t chipSize = parseEnvU32("SAR_PIPELINE_PTA_CHIP", 128);
    pta::PtaChip chip = buildCenterChip(image,
                                        static_cast<int>(chipSize),
                                        static_cast<int>(chipSize));
    auto analysis = analyzer.analyze1DWithZoom(chip, 0);

    nlohmann::json report;
    report["imageRows"] = image.rows();
    report["imageCols"] = image.cols();
    report["pta"]["irw"] = analysis.stats.irw;
    report["pta"]["mslr"] = analysis.stats.mslr;
    report["pta"]["islr"] = analysis.stats.islr;
    report["pta"]["pos"] = analysis.stats.pos;
    report["pta"]["maxPower"] = analysis.stats.maxPower;
    for (const auto& peak : analysis.peaks) {
        report["pta"]["peaks"].push_back({{"index", peak.index}, {"power", peak.power}});
    }

    std::ofstream reportOut(outputDir / "pta_report.json");
    if (reportOut) {
        reportOut << report.dump(2) << "\n";
    }

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
