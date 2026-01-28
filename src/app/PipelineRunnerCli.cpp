#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

#include "app/PipelineRunner.hpp"

namespace {

void printUsage() {
    std::cout << "Usage: pipeline_runner_cli [--rpf <file>] [--sartape <file>] "
                 "[--out <dir>] [--max-lines <n>] [--chip <n>]\n";
}

void setEnv(const std::string& name, const std::string& value) {
#ifdef _WIN32
    _putenv_s(name.c_str(), value.c_str());
#else
    setenv(name.c_str(), value.c_str(), 1);
#endif
}

}  // namespace

int main(int argc, char** argv) {
    std::string rpfPath;
    std::string sarTapePath;
    std::string outputDir;
    std::string maxLines;
    std::string chipSize;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--rpf" && i + 1 < argc) {
            rpfPath = argv[++i];
        } else if (arg == "--sartape" && i + 1 < argc) {
            sarTapePath = argv[++i];
        } else if (arg == "--out" && i + 1 < argc) {
            outputDir = argv[++i];
        } else if (arg == "--max-lines" && i + 1 < argc) {
            maxLines = argv[++i];
        } else if (arg == "--chip" && i + 1 < argc) {
            chipSize = argv[++i];
        } else if (arg == "--help" || arg == "-h") {
            printUsage();
            return 0;
        } else {
            std::cerr << "Unknown or incomplete argument: " << arg << "\n";
            printUsage();
            return 1;
        }
    }

    if (rpfPath.empty() && sarTapePath.empty()) {
        std::cerr << "Provide --rpf or --sartape input.\n";
        printUsage();
        return 1;
    }

    if (!rpfPath.empty()) {
        setEnv("SAR_PIPELINE_RPF", rpfPath);
    }
    if (!sarTapePath.empty()) {
        setEnv("SAR_PIPELINE_SARTAPE", sarTapePath);
    }
    if (!outputDir.empty()) {
        setEnv("SAR_PIPELINE_OUTPUT_DIR", outputDir);
    }
    if (!maxLines.empty()) {
        setEnv("SAR_PIPELINE_MAX_LINES", maxLines);
    }
    if (!chipSize.empty()) {
        setEnv("SAR_PIPELINE_PTA_CHIP", chipSize);
    }

    app::PipelineRunner runner;
    const bool ok = runner.run();
    if (!ok) {
        std::cerr << "Pipeline run failed.\n";
        return 1;
    }

    const std::filesystem::path outDir = outputDir.empty()
                                             ? std::filesystem::path("output")
                                             : std::filesystem::path(outputDir);
    std::cout << "Pipeline run complete. Output: " << outDir.string() << "\n";
    return 0;
}
