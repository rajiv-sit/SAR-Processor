#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>

#include "app/PipelineRunner.hpp"

namespace {

void printUsage() {
    std::cout
        << "Usage: rto_visualizer_cli [--endpoint udp://127.0.0.1:5000] "
           "[--width N] [--height N] [--frames N] [--interval-ms N]\n"
           "Publishes a back-projection preview frame to the RTO viewer.\n"
           "[--shared-file <path>] writes frames to a shared memory region instead of UDP.\n"
           "[--cached-raw <path>] loads an existing <name>_backproj.raw image instead of recomputing.\n"
           "Large images are downsampled to fit a single UDP packet.\n";
}

bool readUint(const char* value, std::uint32_t& out) {
    if (!value) {
        return false;
    }
    char* end = nullptr;
    const auto parsed = std::strtoul(value, &end, 10);
    if (!end || *end != '\0') {
        return false;
    }
    out = static_cast<std::uint32_t>(parsed);
    return true;
}

bool readSize(const char* value, std::size_t& out) {
    if (!value) {
        return false;
    }
    char* end = nullptr;
    const auto parsed = std::strtoull(value, &end, 10);
    if (!end || *end != '\0') {
        return false;
    }
    out = static_cast<std::size_t>(parsed);
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    std::string endpoint = "udp://127.0.0.1:5000";
    std::optional<std::filesystem::path> sharedFile;
    std::optional<std::filesystem::path> cachedRaw;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::size_t frames = 120;
    std::uint32_t intervalMs = 33;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            printUsage();
            return 0;
        }
        if (arg == "--endpoint" && i + 1 < argc) {
            endpoint = argv[++i];
            continue;
        }
        if (arg == "--width" && i + 1 < argc) {
            if (!readUint(argv[++i], width)) {
                std::cerr << "Invalid --width value.\n";
                return 1;
            }
            continue;
        }
        if (arg == "--height" && i + 1 < argc) {
            if (!readUint(argv[++i], height)) {
                std::cerr << "Invalid --height value.\n";
                return 1;
            }
            continue;
        }
        if (arg == "--frames" && i + 1 < argc) {
            if (!readSize(argv[++i], frames)) {
                std::cerr << "Invalid --frames value.\n";
                return 1;
            }
            continue;
        }
        if (arg == "--interval-ms" && i + 1 < argc) {
            if (!readUint(argv[++i], intervalMs)) {
                std::cerr << "Invalid --interval-ms value.\n";
                return 1;
            }
            continue;
        }
        if (arg == "--shared-file" && i + 1 < argc) {
            sharedFile = std::filesystem::path(argv[++i]);
            continue;
        }
        if (arg == "--cached-raw" && i + 1 < argc) {
            cachedRaw = std::filesystem::path(argv[++i]);
            continue;
        }

        std::cerr << "Unknown argument: " << arg << "\n";
        printUsage();
        return 1;
    }

    app::PipelineRunner runner;
    const std::string target =
        sharedFile ? sharedFile->string()
                   : cachedRaw ? cachedRaw->string()
                               : endpoint;
    if (!runner.runRtoPreview(endpoint, sharedFile, cachedRaw, width, height, frames, intervalMs)) {
        std::cerr << "Failed to publish frames to " << target << "\n";
        return 1;
    }

    std::cout << "Published " << frames << " frame(s) to " << endpoint << "\n";
    return 0;
}
