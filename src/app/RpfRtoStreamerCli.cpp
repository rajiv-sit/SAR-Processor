#include <chrono>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include <Eigen/Dense>

#include "backproj/BackProjConfigLoader.hpp"
#include "backproj/BackProjectionEngine.hpp"
#include "rto/RtoDataBus.hpp"

namespace {

void printUsage() {
    std::cout << "Usage: rpf_rto_streamer_cli --rpf <file> "
                 "[--endpoint udp://127.0.0.1:5000] [--out <dir>] [--prefix <name>] "
                 "[--width <px>] [--height <px>] [--frames <n>] [--interval-ms <n>]\n";
}

bool parseU32(const std::string& value, std::uint32_t& out) {
    try {
        out = static_cast<std::uint32_t>(std::stoul(value));
        return true;
    } catch (...) {
        return false;
    }
}

std::uint32_t clampU32(std::uint32_t value, std::uint32_t minValue) {
    return value < minValue ? minValue : value;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        std::string rpfPath;
        std::string endpoint = "udp://127.0.0.1:5000";
        std::string outputDir = "output";
        std::string outputPrefix = "rpf_stream";
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        std::uint32_t frames = 1;
        std::uint32_t intervalMs = 0;
        bool fastMode = false;

        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            if (arg == "--rpf" && i + 1 < argc) {
                rpfPath = argv[++i];
        } else if (arg == "--endpoint" && i + 1 < argc) {
            endpoint = argv[++i];
        } else if (arg == "--out" && i + 1 < argc) {
            outputDir = argv[++i];
            } else if (arg == "--prefix" && i + 1 < argc) {
                outputPrefix = argv[++i];
            } else if (arg == "--width" && i + 1 < argc) {
                if (!parseU32(argv[++i], width)) {
                    std::cerr << "Invalid width.\n";
                    return 1;
                }
            } else if (arg == "--height" && i + 1 < argc) {
                if (!parseU32(argv[++i], height)) {
                    std::cerr << "Invalid height.\n";
                    return 1;
                }
            } else if (arg == "--frames" && i + 1 < argc) {
                if (!parseU32(argv[++i], frames)) {
                    std::cerr << "Invalid frames count.\n";
                    return 1;
                }
        } else if (arg == "--interval-ms" && i + 1 < argc) {
            if (!parseU32(argv[++i], intervalMs)) {
                std::cerr << "Invalid interval-ms.\n";
                return 1;
            }
        } else if (arg == "--fast") {
            fastMode = true;
        } else if (arg == "--help" || arg == "-h") {
                printUsage();
                return 0;
            } else {
                std::cerr << "Unknown or incomplete argument: " << arg << "\n";
                printUsage();
                return 1;
            }
        }

        if (rpfPath.empty()) {
            std::cerr << "Provide --rpf input.\n";
            printUsage();
            return 1;
        }

        std::cout << "Loading back-projection configs...\n";
        std::cout.flush();
        backproj::BackProjConfigLoader loader;
        auto operatorConfig = loader.loadOperatorConfig("configs/backproj/operator.json");
        auto secondaryConfig = loader.loadSecondaryConfig("configs/backproj/secondary.json");

        const std::filesystem::path inputPath = rpfPath;
        std::cout << "RPF input: " << inputPath.string() << "\n";
        if (!std::filesystem::exists(inputPath)) {
            std::cout << "RPF input not found: " << inputPath.string() << "\n";
            return 1;
        }
        operatorConfig.inputFilePath = inputPath.has_parent_path()
                                           ? inputPath.parent_path().string()
                                           : std::string();
        operatorConfig.inputFileName = inputPath.filename().string();

        if (width > 0) {
            operatorConfig.nPixX = width;
        }
        if (height > 0) {
            operatorConfig.nPixY = height;
        }

        std::filesystem::path outDirPath = outputDir;
        std::filesystem::create_directories(outDirPath);
        operatorConfig.rpfBaseFileName = (outDirPath / outputPrefix).string();
        operatorConfig.fastMode = fastMode;

        std::cout << "Running back-projection...\n";
        std::cout.flush();
        backproj::BackProjectionEngine engine(operatorConfig, secondaryConfig);
        const Eigen::MatrixXf image = engine.runWithOutputs();
        if (image.size() == 0) {
            std::cout << "Back-projection produced no image. Check configs/backproj/operator.json "
                         "and the RPF input path.\n";
            return 1;
        }
        std::cout << "Image size: " << image.rows() << "x" << image.cols() << "\n";

        if (!fastMode) {
            std::cout << "Streaming to " << endpoint << "...\n";
            rto::RtoDataBus bus(endpoint);
            rto::RtoFrame frame{};
            std::uint32_t outWidth = static_cast<std::uint32_t>(image.cols());
            std::uint32_t outHeight = static_cast<std::uint32_t>(image.rows());

            constexpr std::size_t kUdpMaxPayload = 65507;
            const std::size_t maxPixels = (kUdpMaxPayload - 16) / sizeof(float);
            const std::size_t pixelCount = static_cast<std::size_t>(outWidth) * outHeight;
            if (pixelCount > maxPixels) {
                const double scale =
                    std::sqrt(static_cast<double>(maxPixels) / static_cast<double>(pixelCount));
                outWidth = clampU32(static_cast<std::uint32_t>(outWidth * scale), 1);
                outHeight = clampU32(static_cast<std::uint32_t>(outHeight * scale), 1);
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
                    const int srcY =
                        static_cast<int>(static_cast<std::size_t>(y) * image.rows() / outHeight);
                    for (std::uint32_t x = 0; x < outWidth; ++x) {
                        const int srcX =
                            static_cast<int>(static_cast<std::size_t>(x) * image.cols() / outWidth);
                        frame.pixels[static_cast<std::size_t>(y) * outWidth + x] = image(srcY, srcX);
                    }
                }
            }

            const std::uint32_t emitFrames = frames == 0 ? 1u : frames;
            for (std::uint32_t frameIndex = 0; frameIndex < emitFrames; ++frameIndex) {
                frame.timestampNs = static_cast<std::uint64_t>(
                    std::chrono::duration_cast<std::chrono::nanoseconds>(
                        std::chrono::system_clock::now().time_since_epoch())
                        .count());

                if (!bus.publish(frame)) {
                    std::cout << "Failed to publish UDP frame to " << endpoint << ".\n";
                    return 1;
                }

                if (frameIndex + 1 < emitFrames && intervalMs > 0) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(intervalMs));
                }
            }

            std::cout << "Streamed " << (frames == 0 ? 1u : frames) << " frame(s) to " << endpoint
                      << ". Output prefix: " << operatorConfig.rpfBaseFileName << "\n";
        } else {
            std::cout << "Fast mode: skipping UDP streaming.\n";
        }
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "Fatal error: " << ex.what() << "\n";
        return 1;
    } catch (...) {
        std::cerr << "Fatal error: unknown exception.\n";
        return 1;
    }
}
