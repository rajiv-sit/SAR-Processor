#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <imgui.h>
// Use the repo-local Dear ImGui backend bindings (under SAR-Processor/bindings).
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <limits>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "rpf/RpfProductStreamLine.hpp"

namespace {

struct Image {
    std::uint32_t width{};
    std::uint32_t height{};
    std::vector<float> pixels;  // row-major
};

enum class ColorMode {
    kGrayscale = 0,
    kGrayscaleInverted = 1,
    kHot = 2,
    kTurbo = 3,
};

struct FramePacket {
    std::uint64_t version = 0;
    std::uint64_t timestampNs = 0;
    std::string name;
    std::uint32_t fileIndex = 0;  // 1-based index within the current scan (0 if unknown)
    std::uint32_t fileCount = 0;  // total files in the current scan (0 if unknown)
    std::uint32_t rpfFrame = 0;   // 1-based RPF frame number used for this image (0 if unknown)
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<float> pixels;
    float minV = 0.0f;
    float maxV = 0.0f;
};

class FrameStore {
public:
    void set(FramePacket pkt) {
        std::lock_guard<std::mutex> lock(mu_);
        latest_ = std::move(pkt);
        hasLatest_ = true;
    }

    bool getIfNew(std::uint64_t lastVersion, FramePacket& out) const {
        std::lock_guard<std::mutex> lock(mu_);
        if (!hasLatest_) {
            return false;
        }
        if (latest_.version == lastVersion) {
            return false;
        }
        out = latest_;  // copy (pixels are 512x512 by default; OK for UI thread)
        return true;
    }

    bool peek(FramePacket& out) const {
        std::lock_guard<std::mutex> lock(mu_);
        if (!hasLatest_) return false;
        out = latest_;
        return true;
    }

private:
    mutable std::mutex mu_;
    FramePacket latest_{};
    bool hasLatest_ = false;
};

bool initGlfw() {
    if (!glfwInit()) {
        return false;
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif
    return true;
}

std::uint64_t nowTimestampNs() {
    // Monotonic timestamp for UI "age" calculations (won't jump if system time changes).
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

void printUsage() {
    std::cout
        << "Usage: sar_imgui_rpf_viewer_cli --input-dir <dir> [--frame N] [--max N]\n"
           "                               [--frame-mode fixed|file-index]\n"
           "                               [--preview-width W] [--preview-height H]\n"
           "                               [--publish-sleep-ms N] [--loop]\n"
           "Single-process RPF reader + ImGui visualizer. Reads .rpf files and displays the latest preview.\n";
}

static bool splitFlagValue(const std::string& arg, const std::string& prefix, std::string& outValue) {
    // Accept both "--flag value" and "--flag=value" forms.
    if (arg == prefix) {
        outValue.clear();
        return true;
    }
    const std::string withEq = prefix + "=";
    if (arg.rfind(withEq, 0) == 0) {
        outValue = arg.substr(withEq.size());
        return true;
    }
    return false;
}

static std::uint8_t clampU8(int v) {
    if (v < 0) return 0;
    if (v > 255) return 255;
    return static_cast<std::uint8_t>(v);
}

static void turboColormap(std::uint8_t x, std::uint8_t& r, std::uint8_t& g, std::uint8_t& b) {
    // Lightweight approximation of Google's "Turbo" colormap using a small LUT.
    // This is intentionally tiny; good enough for a viewer without pulling in large tables.
    // Keypoints in sRGB (t in [0..1]): (0.0)->(48,18,59), (0.25)->(50,126,184),
    // (0.5)->(179,221,55), (0.75)->(250,174,34), (1.0)->(122,4,2)
    const float t = static_cast<float>(x) / 255.0f;
    struct K { float t; int r,g,b; };
    constexpr K k[] = {
        {0.00f,  48,  18,  59},
        {0.25f,  50, 126, 184},
        {0.50f, 179, 221,  55},
        {0.75f, 250, 174,  34},
        {1.00f, 122,   4,   2},
    };
    const K* a = &k[0];
    const K* bkp = &k[1];
    for (int i = 0; i < 4; ++i) {
        if (t >= k[i].t && t <= k[i + 1].t) {
            a = &k[i];
            bkp = &k[i + 1];
            break;
        }
    }
    const float span = (bkp->t - a->t) > 0.0f ? (bkp->t - a->t) : 1.0f;
    const float u = (t - a->t) / span;
    r = clampU8(static_cast<int>(a->r + (bkp->r - a->r) * u + 0.5f));
    g = clampU8(static_cast<int>(a->g + (bkp->g - a->g) * u + 0.5f));
    b = clampU8(static_cast<int>(a->b + (bkp->b - a->b) * u + 0.5f));
}

static void hotColormap(std::uint8_t x, std::uint8_t& r, std::uint8_t& g, std::uint8_t& b) {
    // Classic "hot" palette: black -> red -> yellow -> white.
    const int v = static_cast<int>(x);
    const int r0 = std::min(255, v * 3);
    const int g0 = std::min(255, std::max(0, (v - 85) * 3));
    const int b0 = std::min(255, std::max(0, (v - 170) * 3));
    r = static_cast<std::uint8_t>(r0);
    g = static_cast<std::uint8_t>(g0);
    b = static_cast<std::uint8_t>(b0);
}

static void mapToRGBA(const std::uint8_t* gray, std::size_t pixelCount, ColorMode mode, std::vector<std::uint8_t>& outRgba) {
    outRgba.resize(pixelCount * 4);
    for (std::size_t i = 0; i < pixelCount; ++i) {
        std::uint8_t v = gray[i];
        if (mode == ColorMode::kGrayscaleInverted) {
            v = static_cast<std::uint8_t>(255u - v);
        }
        std::uint8_t r = v, g = v, b = v;
        switch (mode) {
        case ColorMode::kGrayscale:
        case ColorMode::kGrayscaleInverted:
            break;
        case ColorMode::kHot:
            hotColormap(v, r, g, b);
            break;
        case ColorMode::kTurbo:
            turboColormap(v, r, g, b);
            break;
        }
        outRgba[i * 4 + 0] = r;
        outRgba[i * 4 + 1] = g;
        outRgba[i * 4 + 2] = b;
        outRgba[i * 4 + 3] = 255;
    }
}

static bool buildPreviewFromRpf(const std::string& rpfPath,
                                int frameNum,
                                std::uint32_t previewWidth,
                                std::uint32_t previewHeight,
                                Image& out,
                                float& outMin,
                                float& outMax,
                                std::string& err) {
    rpf::RpfProductStreamLine stream;
    if (!rpf::RpfProductStreamLine::init(rpfPath, frameNum, stream, err)) {
        return false;
    }

    const auto& blocks = stream.blocks();
    if (blocks.empty()) {
        err = "No RPF blocks found: " + rpfPath;
        return false;
    }
    std::uint32_t width = static_cast<std::uint32_t>(blocks.front().numPixels);
    std::uint32_t height = 0;
    for (const auto& b : blocks) {
        height += static_cast<std::uint32_t>(b.numLines);
        if (static_cast<std::uint32_t>(b.numPixels) != width) {
            err = "Mixed numPixels across blocks not supported: " + rpfPath;
            return false;
        }
    }
    if (width == 0 || height == 0) {
        err = "Invalid dims: " + rpfPath;
        return false;
    }

    // Reading a full RPF into memory can be very slow (and huge). For the UI we only need a preview,
    // so sample nearest-neighbor at the requested preview resolution by reading only the needed lines.
    const std::uint32_t outW = std::max<std::uint32_t>(1u, std::min(previewWidth, width));
    const std::uint32_t outH = std::max<std::uint32_t>(1u, std::min(previewHeight, height));

    out.width = outW;
    out.height = outH;
    out.pixels.assign(static_cast<std::size_t>(outW) * outH, 0.0f);

    std::vector<float> line;
    line.reserve(width);

    outMin = std::numeric_limits<float>::infinity();
    outMax = -std::numeric_limits<float>::infinity();

    for (std::uint32_t y = 0; y < outH; ++y) {
        const std::uint32_t srcY = static_cast<std::uint32_t>(
            (static_cast<std::size_t>(y) * height) / outH);
        const int lineNum = static_cast<int>(srcY) + 1;

        if (!stream.readLine(lineNum, line)) {
            err = "Failed to read line " + std::to_string(lineNum) + " from " + rpfPath;
            return false;
        }
        if (line.size() != width) {
            err = "Unexpected line width " + std::to_string(line.size()) + " from " + rpfPath;
            return false;
        }

        const std::size_t rowBase = static_cast<std::size_t>(y) * outW;
        for (std::uint32_t x = 0; x < outW; ++x) {
            const std::uint32_t srcX = static_cast<std::uint32_t>(
                (static_cast<std::size_t>(x) * width) / outW);
            const float v = line[static_cast<std::size_t>(srcX)];
            out.pixels[rowBase + x] = v;
            outMin = std::min(outMin, v);
            outMax = std::max(outMax, v);
        }
    }
    return true;
}

static void producerThreadFn(FrameStore* store,
                             std::filesystem::path inputDir,
                             int baseFrameNum,
                             bool frameFromFileIndex,
                             int maxFiles,
                             std::uint32_t previewWidth,
                             std::uint32_t previewHeight,
                             std::uint32_t publishSleepMs,
                             bool loop,
                             std::atomic<bool>* stopFlag) {
    std::error_code ec;
    std::vector<std::filesystem::path> files;
    std::uint64_t version = 0;  // must be monotonic across --loop; UI uses it to detect new frames
    for (;;) {
        // If the directory doesn't exist, publish a status frame so the UI isn't blank.
        if (!std::filesystem::exists(inputDir, ec)) {
            FramePacket pkt;
            pkt.version = ++version;
            pkt.timestampNs = nowTimestampNs();
            pkt.name = "WAITING: input dir not found";
            pkt.rpfFrame = static_cast<std::uint32_t>(std::max(1, baseFrameNum));
            pkt.width = 1;
            pkt.height = 1;
            pkt.pixels = {0.0f};
            pkt.minV = 0.0f;
            pkt.maxV = 0.0f;
            store->set(std::move(pkt));

            std::this_thread::sleep_for(std::chrono::milliseconds(250));
            if (!loop || stopFlag->load(std::memory_order_relaxed)) {
                return;
            }
            continue;
        }

        files.clear();
        for (auto it = std::filesystem::recursive_directory_iterator(inputDir, ec);
             it != std::filesystem::recursive_directory_iterator();
             ++it) {
            if (it->is_regular_file()) {
                const auto p = it->path();
                if (p.extension() == ".rpf") {
                    files.push_back(p);
                }
            }
        }
        std::sort(files.begin(), files.end());
        if (maxFiles > 0 && static_cast<int>(files.size()) > maxFiles) {
            files.resize(static_cast<std::size_t>(maxFiles));
        }

        if (files.empty()) {
            FramePacket pkt;
            pkt.version = ++version;
            pkt.timestampNs = nowTimestampNs();
            pkt.name = "WAITING: no .rpf files found";
            pkt.rpfFrame = static_cast<std::uint32_t>(std::max(1, baseFrameNum));
            pkt.width = 1;
            pkt.height = 1;
            pkt.pixels = {0.0f};
            pkt.minV = 0.0f;
            pkt.maxV = 0.0f;
            store->set(std::move(pkt));

            std::this_thread::sleep_for(std::chrono::milliseconds(250));
            if (!loop || stopFlag->load(std::memory_order_relaxed)) {
                return;
            }
            continue;
        }

        for (std::size_t i = 0; i < files.size(); ++i) {
            if (stopFlag->load(std::memory_order_relaxed)) {
                return;
            }

            const auto& path = files[i];
            const std::string stem = path.stem().string();

            const int frameToRead = frameFromFileIndex
                                        ? std::max(1, baseFrameNum + static_cast<int>(i))
                                        : std::max(1, baseFrameNum);

            std::string err;
            Image preview = {};
            float minV = 0.0f;
            float maxV = 0.0f;
            if (!buildPreviewFromRpf(path.string(),
                                     frameToRead,
                                     previewWidth,
                                     previewHeight,
                                     preview,
                                     minV,
                                     maxV,
                                      err)) {
                // Publish a small status packet so the UI shows the failure.
                FramePacket pkt;
                pkt.version = ++version;
                pkt.timestampNs = nowTimestampNs();
                pkt.fileIndex = static_cast<std::uint32_t>(i + 1);
                pkt.fileCount = static_cast<std::uint32_t>(files.size());
                pkt.rpfFrame = static_cast<std::uint32_t>(frameToRead);
                pkt.name = "ERROR: " + stem;
                pkt.width = 1;
                pkt.height = 1;
                pkt.pixels = {0.0f};
                pkt.minV = 0.0f;
                pkt.maxV = 0.0f;
                store->set(std::move(pkt));
                if (publishSleepMs > 0) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(publishSleepMs));
                }
                continue;
            }

            FramePacket pkt;
            pkt.version = ++version;
            pkt.timestampNs = nowTimestampNs();
            pkt.name = stem;
            pkt.fileIndex = static_cast<std::uint32_t>(i + 1);
            pkt.fileCount = static_cast<std::uint32_t>(files.size());
            pkt.rpfFrame = static_cast<std::uint32_t>(frameToRead);
            pkt.width = preview.width;
            pkt.height = preview.height;
            pkt.pixels = std::move(preview.pixels);
            pkt.minV = minV;
            pkt.maxV = maxV;
            store->set(std::move(pkt));

            if (publishSleepMs > 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(publishSleepMs));
            }
        }

        if (!loop) {
            return;
        }
    }
}

}  // namespace

int main(int argc, char** argv) {
    std::filesystem::path inputDir;
    int frameNum = 1;
    bool frameFromFileIndex = false;
    int maxFiles = 0;  // 0 = all
    std::uint32_t previewWidth = 512;
    std::uint32_t previewHeight = 512;
    std::uint32_t publishSleepMs = 0;
    bool loop = false;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            printUsage();
            return 0;
        }

        std::string value;
        if (splitFlagValue(arg, "--input-dir", value)) {
            if (value.empty() && i + 1 < argc) value = argv[++i];
            if (value.empty()) {
                std::cerr << "Missing value for --input-dir\n";
                return 2;
            }
            inputDir = value;
            continue;
        }
        if (splitFlagValue(arg, "--frame", value)) {
            if (value.empty() && i + 1 < argc) value = argv[++i];
            if (value.empty()) {
                std::cerr << "Missing value for --frame\n";
                return 2;
            }
            frameNum = std::max(1, std::stoi(value));
            continue;
        }
        if (splitFlagValue(arg, "--frame-mode", value)) {
            if (value.empty() && i + 1 < argc) value = argv[++i];
            if (value.empty()) {
                std::cerr << "Missing value for --frame-mode\n";
                return 2;
            }
            if (value == "fixed") {
                frameFromFileIndex = false;
            } else if (value == "file-index") {
                // Matches the effect of the user's old "frameNum = i" tweak, but keeps semantics explicit.
                // With --frame N, the used frame becomes N + fileIndex-1.
                frameFromFileIndex = true;
            } else {
                std::cerr << "Invalid --frame-mode: " << value << " (expected fixed|file-index)\n";
                return 2;
            }
            continue;
        }
        if (splitFlagValue(arg, "--max", value)) {
            if (value.empty() && i + 1 < argc) value = argv[++i];
            if (value.empty()) {
                std::cerr << "Missing value for --max\n";
                return 2;
            }
            maxFiles = std::max(0, std::stoi(value));
            continue;
        }
        if (splitFlagValue(arg, "--preview-width", value)) {
            if (value.empty() && i + 1 < argc) value = argv[++i];
            if (value.empty()) {
                std::cerr << "Missing value for --preview-width\n";
                return 2;
            }
            previewWidth = static_cast<std::uint32_t>(std::max(1, std::stoi(value)));
            continue;
        }
        if (splitFlagValue(arg, "--preview-height", value)) {
            if (value.empty() && i + 1 < argc) value = argv[++i];
            if (value.empty()) {
                std::cerr << "Missing value for --preview-height\n";
                return 2;
            }
            previewHeight = static_cast<std::uint32_t>(std::max(1, std::stoi(value)));
            continue;
        }
        if (splitFlagValue(arg, "--publish-sleep-ms", value)) {
            if (value.empty() && i + 1 < argc) value = argv[++i];
            if (value.empty()) {
                std::cerr << "Missing value for --publish-sleep-ms\n";
                return 2;
            }
            publishSleepMs = static_cast<std::uint32_t>(std::max(0, std::stoi(value)));
            continue;
        }

        if (arg == "--loop") {
            loop = true;
            continue;
        }
        std::cerr << "Unknown/incomplete arg: " << arg << "\n";
        printUsage();
        return 2;
    }

    if (inputDir.empty()) {
        printUsage();
        return 2;
    }

    if (!initGlfw()) {
        std::cerr << "Failed to initialize GLFW.\n";
        return 1;
    }

    GLFWwindow* window = glfwCreateWindow(1280, 720, "SAR RPF ImGui Viewer (Single Process)", nullptr, nullptr);
    if (!window) {
        glfwTerminate();
        std::cerr << "Failed to create GLFW window.\n";
        return 1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    if (glewInit() != GLEW_OK) {
        std::cerr << "Failed to initialize GLEW.\n";
        return 1;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    (void)io;

    ImGui::StyleColorsDark();
    {
        // Slightly more polished default style without pulling external fonts/themes.
        ImGuiStyle& style = ImGui::GetStyle();
        style.WindowRounding = 10.0f;
        style.ChildRounding = 10.0f;
        style.FrameRounding = 8.0f;
        style.GrabRounding = 8.0f;
        style.ScrollbarRounding = 10.0f;
        style.PopupRounding = 10.0f;
        style.WindowPadding = ImVec2(12.0f, 12.0f);
        style.FramePadding = ImVec2(10.0f, 7.0f);
        style.ItemSpacing = ImVec2(10.0f, 8.0f);
        style.ItemInnerSpacing = ImVec2(8.0f, 6.0f);
        style.ScrollbarSize = 14.0f;

        ImVec4* colors = style.Colors;
        // App chrome/background: dark blue (keep image view darker/neutral via per-window overrides below).
        colors[ImGuiCol_WindowBg] = ImVec4(0.03f, 0.07f, 0.13f, 1.00f);
        colors[ImGuiCol_ChildBg] = ImVec4(0.03f, 0.07f, 0.13f, 1.00f);
        colors[ImGuiCol_PopupBg] = ImVec4(0.08f, 0.09f, 0.10f, 0.98f);
        colors[ImGuiCol_Border] = ImVec4(0.16f, 0.18f, 0.20f, 1.00f);
        colors[ImGuiCol_FrameBg] = ImVec4(0.08f, 0.12f, 0.18f, 1.00f);
        colors[ImGuiCol_FrameBgHovered] = ImVec4(0.10f, 0.15f, 0.23f, 1.00f);
        colors[ImGuiCol_FrameBgActive] = ImVec4(0.12f, 0.18f, 0.27f, 1.00f);
        colors[ImGuiCol_TitleBg] = ImVec4(0.05f, 0.06f, 0.07f, 1.00f);
        colors[ImGuiCol_TitleBgActive] = ImVec4(0.09f, 0.10f, 0.12f, 1.00f);
        colors[ImGuiCol_Header] = ImVec4(0.10f, 0.15f, 0.23f, 1.00f);
        colors[ImGuiCol_HeaderHovered] = ImVec4(0.12f, 0.18f, 0.28f, 1.00f);
        colors[ImGuiCol_HeaderActive] = ImVec4(0.14f, 0.21f, 0.33f, 1.00f);
        colors[ImGuiCol_Button] = ImVec4(0.10f, 0.15f, 0.23f, 1.00f);
        colors[ImGuiCol_ButtonHovered] = ImVec4(0.12f, 0.18f, 0.28f, 1.00f);
        colors[ImGuiCol_ButtonActive] = ImVec4(0.14f, 0.21f, 0.33f, 1.00f);
        colors[ImGuiCol_Tab] = ImVec4(0.10f, 0.11f, 0.13f, 1.00f);
        colors[ImGuiCol_TabHovered] = ImVec4(0.20f, 0.22f, 0.26f, 1.00f);
        colors[ImGuiCol_TabActive] = ImVec4(0.14f, 0.16f, 0.19f, 1.00f);
        colors[ImGuiCol_Separator] = ImVec4(0.16f, 0.18f, 0.20f, 1.00f);
        colors[ImGuiCol_CheckMark] = ImVec4(0.85f, 0.87f, 0.90f, 1.00f);
        colors[ImGuiCol_SliderGrab] = ImVec4(0.65f, 0.68f, 0.72f, 1.00f);
        colors[ImGuiCol_SliderGrabActive] = ImVec4(0.85f, 0.87f, 0.90f, 1.00f);
    }
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330 core");

    // Two textures:
    // 1) float texture for raw values (often visually saturates if values are outside [0,1])
    // 2) normalized 8-bit grayscale texture for reliable visualization
    GLuint textureFloat = 0;
    GLuint textureNorm = 0;
    GLuint textureRgba = 0;
    glGenTextures(1, &textureFloat);
    glBindTexture(GL_TEXTURE_2D, textureFloat);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    // Swizzle R -> RGB so ImGui shows grayscale instead of red-only.
    const GLint swizzleRGBA[4] = {GL_RED, GL_RED, GL_RED, GL_ONE};
    glTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_RGBA, swizzleRGBA);
    glBindTexture(GL_TEXTURE_2D, 0);

    glGenTextures(1, &textureNorm);
    glBindTexture(GL_TEXTURE_2D, textureNorm);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_RGBA, swizzleRGBA);
    glBindTexture(GL_TEXTURE_2D, 0);

    glGenTextures(1, &textureRgba);
    glBindTexture(GL_TEXTURE_2D, textureRgba);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    FrameStore store;
    std::atomic<bool> stop{false};
    std::thread producer(producerThreadFn,
                         &store,
                         inputDir,
                         frameNum,
                         frameFromFileIndex,
                         maxFiles,
                         previewWidth,
                         previewHeight,
                         publishSleepMs,
                         loop,
                         &stop);

    std::uint64_t lastVer = 0;
    FramePacket latest;
    std::vector<std::uint8_t> normPixels;
    std::vector<std::uint8_t> rgbaPixels;

    std::uint64_t updates = 0;
    auto fpsWindowStart = std::chrono::steady_clock::now();
    std::uint64_t fpsWindowUpdates = 0;
    double fps = 0.0;
    bool showNormalized = true;  // default: robust SAR-style visualization
    ColorMode colorMode = ColorMode::kGrayscale;
    ColorMode lastColorMode = colorMode;

    bool fitToWindow = true;
    float zoom = 1.0f;  // only used when fitToWindow=false
    const float zoomMin = 0.05f;
    const float zoomMax = 20.0f;
    bool showStats = true;
    bool showHelp = false;

    // Histogram of normalized pixels (0..255).
    std::array<float, 256> hist = {};
    bool histValid = false;

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        const bool updated = store.getIfNew(lastVer, latest);
        const bool modeChanged = (lastColorMode != colorMode);
        if (updated) {
            lastVer = latest.version;
            ++updates;
            ++fpsWindowUpdates;

            // Upload raw float texture.
            glBindTexture(GL_TEXTURE_2D, textureFloat);
            glTexImage2D(GL_TEXTURE_2D,
                         0,
                         GL_R32F,
                         static_cast<GLsizei>(latest.width),
                         static_cast<GLsizei>(latest.height),
                         0,
                         GL_RED,
                         GL_FLOAT,
                         latest.pixels.data());
            glBindTexture(GL_TEXTURE_2D, 0);

            // Upload normalized 8-bit texture (maps [min,max] -> [0,255]).
            const std::size_t pixelCount =
                static_cast<std::size_t>(latest.width) * static_cast<std::size_t>(latest.height);
            normPixels.resize(pixelCount);
            const float minV = latest.minV;
            const float maxV = latest.maxV;
            const float range = (maxV > minV) ? (maxV - minV) : 1.0f;
            for (std::size_t idx = 0; idx < pixelCount; ++idx) {
                float n = (latest.pixels[idx] - minV) / range;
                n = std::clamp(n, 0.0f, 1.0f);
                normPixels[idx] = static_cast<std::uint8_t>(n * 255.0f + 0.5f);
            }
            glBindTexture(GL_TEXTURE_2D, textureNorm);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glTexImage2D(GL_TEXTURE_2D,
                         0,
                         GL_R8,
                         static_cast<GLsizei>(latest.width),
                         static_cast<GLsizei>(latest.height),
                         0,
                         GL_RED,
                         GL_UNSIGNED_BYTE,
                         normPixels.data());
            glBindTexture(GL_TEXTURE_2D, 0);

            // Also prepare/upload RGBA texture for color modes (generated from normalized pixels).
            mapToRGBA(normPixels.data(), pixelCount, colorMode, rgbaPixels);
            glBindTexture(GL_TEXTURE_2D, textureRgba);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glTexImage2D(GL_TEXTURE_2D,
                         0,
                         GL_RGBA8,
                         static_cast<GLsizei>(latest.width),
                         static_cast<GLsizei>(latest.height),
                         0,
                         GL_RGBA,
                         GL_UNSIGNED_BYTE,
                         rgbaPixels.data());
            glBindTexture(GL_TEXTURE_2D, 0);

            // Update histogram.
            hist.fill(0.0f);
            for (std::uint8_t v : normPixels) {
                hist[static_cast<std::size_t>(v)] += 1.0f;
            }
            const float invN = pixelCount > 0 ? (1.0f / static_cast<float>(pixelCount)) : 0.0f;
            for (float& h : hist) h *= invN;
            histValid = true;
        } else if (modeChanged && !normPixels.empty() && latest.width > 0 && latest.height > 0) {
            // Recolor without waiting for a new frame.
            const std::size_t pixelCount =
                static_cast<std::size_t>(latest.width) * static_cast<std::size_t>(latest.height);
            mapToRGBA(normPixels.data(), pixelCount, colorMode, rgbaPixels);
            glBindTexture(GL_TEXTURE_2D, textureRgba);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glTexImage2D(GL_TEXTURE_2D,
                         0,
                         GL_RGBA8,
                         static_cast<GLsizei>(latest.width),
                         static_cast<GLsizei>(latest.height),
                         0,
                         GL_RGBA,
                         GL_UNSIGNED_BYTE,
                         rgbaPixels.data());
            glBindTexture(GL_TEXTURE_2D, 0);
        }
        lastColorMode = colorMode;

        const auto nowSteady = std::chrono::steady_clock::now();
        const auto fpsElapsed =
            std::chrono::duration_cast<std::chrono::milliseconds>(nowSteady - fpsWindowStart);
        if (fpsElapsed.count() >= 1000) {
            fps = static_cast<double>(fpsWindowUpdates) * 1000.0 /
                  static_cast<double>(fpsElapsed.count());
            fpsWindowStart = nowSteady;
            fpsWindowUpdates = 0;
        }

        // Single, stable layout (no docking APIs needed): left "Display" panel and right image view.
        {
            ImGuiWindowFlags rootFlags = ImGuiWindowFlags_NoTitleBar |
                                         ImGuiWindowFlags_NoCollapse |
                                         ImGuiWindowFlags_NoResize |
                                         ImGuiWindowFlags_NoMove |
                                         ImGuiWindowFlags_NoBringToFrontOnFocus |
                                         ImGuiWindowFlags_NoNavFocus |
                                         ImGuiWindowFlags_MenuBar;
            const ImGuiViewport* vp = ImGui::GetMainViewport();
            ImGui::SetNextWindowPos(vp->WorkPos);
            ImGui::SetNextWindowSize(vp->WorkSize);
            ImGui::SetNextWindowViewport(vp->ID);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 12.0f));
            ImGui::Begin("SAR Viewer", nullptr, rootFlags);
            ImGui::PopStyleVar(3);

            if (ImGui::BeginMenuBar()) {
                if (ImGui::BeginMenu("View")) {
                    ImGui::MenuItem("Stats", nullptr, &showStats);
                    ImGui::MenuItem("Help", nullptr, &showHelp);
                    ImGui::EndMenu();
                }
                ImGui::EndMenuBar();
            }

            if (ImGui::BeginTable("layout", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV)) {
                ImGui::TableSetupColumn("Display", ImGuiTableColumnFlags_WidthFixed, 380.0f);
                ImGui::TableSetupColumn("Image", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableNextRow();

                // Left panel: display + controls + histogram.
                ImGui::TableSetColumnIndex(0);
                ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.03f, 0.07f, 0.13f, 1.00f)); // dark blue
                ImGui::BeginChild("display_panel", ImVec2(0, 0), false);
                ImGui::TextUnformatted("Display");
                ImGui::Separator();

                ImGui::Text("Input: %s", inputDir.string().c_str());
                if (latest.fileIndex > 0 && latest.fileCount > 0) {
                    ImGui::Text("File: %u / %u", latest.fileIndex, latest.fileCount);
                } else {
                    ImGui::TextUnformatted("File: n/a");
                }
                ImGui::Text("Name: %s", latest.name.empty() ? "n/a" : latest.name.c_str());
                ImGui::Text("RPF frame arg: %d  Mode: %s", frameNum, frameFromFileIndex ? "file-index" : "fixed");
                if (latest.rpfFrame > 0) ImGui::Text("RPF frame used: %u", latest.rpfFrame);

                if (showStats) {
                    ImGui::Separator();
                    ImGui::Text("Resolution: %ux%u", latest.width, latest.height);
                    ImGui::Text("Min/Max: %.6g / %.6g", latest.minV, latest.maxV);
                    if (latest.timestampNs != 0) {
                        const std::uint64_t ageNs = nowTimestampNs() - latest.timestampNs;
                        ImGui::Text("Age: %.1f ms", static_cast<double>(ageNs) / 1e6);
                    } else {
                        ImGui::TextUnformatted("Age: n/a");
                    }
                    ImGui::Text("Updates: %llu  FPS: %.2f", static_cast<unsigned long long>(updates), fps);
                    ImGui::Text("Updated: %s", updated ? "yes" : "no");
                }

                ImGui::Separator();
                ImGui::TextUnformatted("Image Settings");
                ImGui::Checkbox("Show normalized (recommended)", &showNormalized);

                const char* colorModeLabel = "";
                switch (colorMode) {
                case ColorMode::kGrayscale: colorModeLabel = "Grayscale"; break;
                case ColorMode::kGrayscaleInverted: colorModeLabel = "Grayscale (inverted)"; break;
                case ColorMode::kHot: colorModeLabel = "Hot"; break;
                case ColorMode::kTurbo: colorModeLabel = "Turbo"; break;
                }
                if (ImGui::BeginCombo("Color", colorModeLabel)) {
                    const auto pick = [&](ColorMode m, const char* label) {
                        const bool selected = (colorMode == m);
                        if (ImGui::Selectable(label, selected)) colorMode = m;
                        if (selected) ImGui::SetItemDefaultFocus();
                    };
                    pick(ColorMode::kGrayscale, "Grayscale");
                    pick(ColorMode::kGrayscaleInverted, "Grayscale (inverted)");
                    pick(ColorMode::kHot, "Hot");
                    pick(ColorMode::kTurbo, "Turbo");
                    ImGui::EndCombo();
                }

                ImGui::Checkbox("Fit to window", &fitToWindow);
                if (!fitToWindow) {
                    ImGui::SliderFloat("Zoom", &zoom, zoomMin, zoomMax, "%.2fx", ImGuiSliderFlags_Logarithmic);
                } else {
                    ImGui::TextUnformatted("Wheel zoom disables fit mode.");
                }

                ImGui::Separator();
                ImGui::TextUnformatted("Histogram");
                if (histValid) {
                    ImGui::PlotHistogram("##hist",
                                         hist.data(),
                                         static_cast<int>(hist.size()),
                                         0,
                                         nullptr,
                                         0.0f,
                                         0.03f,
                                         ImVec2(-1, 120));
                } else {
                    ImGui::TextUnformatted("Waiting for data...");
                }

                if (showHelp) {
                    ImGui::Separator();
                    ImGui::TextUnformatted("Help");
                    ImGui::BulletText("Wheel: zoom (in image view)");
                    ImGui::BulletText("Right/Middle drag: pan");
                    ImGui::BulletText("Use Grayscale for classic SAR look");
                }

                ImGui::EndChild();
                ImGui::PopStyleColor();

                // Right panel: image view.
                ImGui::TableSetColumnIndex(1);
                ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.03f, 0.07f, 0.13f, 1.00f)); // dark blue chrome
                ImGui::BeginChild("image_panel", ImVec2(0, 0), false);
                ImGui::TextUnformatted("Image");
                ImGui::Separator();

                const GLuint activeTex =
                    showNormalized ? (colorMode == ColorMode::kGrayscale ? textureNorm : textureRgba)
                                   : textureFloat;

                if (activeTex != 0 && latest.width > 0 && latest.height > 0 && !latest.pixels.empty()) {
                    ImVec2 avail = ImGui::GetContentRegionAvail();
                    const ImVec2 childSize = ImVec2(avail.x, std::max(200.0f, avail.y));
                    // Image viewport itself: neutral near-black, separate from blue chrome.
                    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.02f, 0.02f, 0.02f, 1.00f));
                    ImGui::BeginChild("image_view", childSize, true, ImGuiWindowFlags_HorizontalScrollbar);

            auto computeFitScale = [&]() -> float {
                const ImVec2 inner = ImGui::GetContentRegionAvail();
                const float sx = inner.x / static_cast<float>(latest.width);
                const float sy = inner.y / static_cast<float>(latest.height);
                return std::max(zoomMin, std::min(sx, sy));
            };

            float scale = fitToWindow ? computeFitScale() : zoom;
            if (!fitToWindow) {
                scale = std::clamp(scale, zoomMin, zoomMax);
            }

            const bool hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
            const float wheel = hovered ? ImGui::GetIO().MouseWheel : 0.0f;

            if (hovered && (ImGui::IsMouseDragging(ImGuiMouseButton_Right) || ImGui::IsMouseDragging(ImGuiMouseButton_Middle))) {
                const ImVec2 delta = ImGui::GetIO().MouseDelta;
                ImGui::SetScrollX(ImGui::GetScrollX() - delta.x);
                ImGui::SetScrollY(ImGui::GetScrollY() - delta.y);
            }

            const ImVec2 imageTopLeft = ImGui::GetCursorScreenPos();
            if (wheel != 0.0f) {
                const float oldScale = scale;
                if (fitToWindow) {
                    fitToWindow = false;
                    zoom = oldScale;
                }
                const float factor = std::pow(1.2f, wheel);
                zoom = std::clamp(zoom * factor, zoomMin, zoomMax);
                scale = zoom;

                const ImVec2 mouse = ImGui::GetIO().MousePos;
                const float viewX = mouse.x - imageTopLeft.x;
                const float viewY = mouse.y - imageTopLeft.y;
                const float contentX = viewX + ImGui::GetScrollX();
                const float contentY = viewY + ImGui::GetScrollY();

                const float imgW0 = static_cast<float>(latest.width) * oldScale;
                const float imgH0 = static_cast<float>(latest.height) * oldScale;
                const float u = imgW0 > 0.0f ? (contentX / imgW0) : 0.0f;
                const float v = imgH0 > 0.0f ? (contentY / imgH0) : 0.0f;

                const float imgW1 = static_cast<float>(latest.width) * scale;
                const float imgH1 = static_cast<float>(latest.height) * scale;
                ImGui::SetScrollX(u * imgW1 - viewX);
                ImGui::SetScrollY(v * imgH1 - viewY);
            }

            const ImVec2 imageSize = {
                static_cast<float>(latest.width) * scale,
                static_cast<float>(latest.height) * scale};
            ImGui::Image(reinterpret_cast<void*>(static_cast<uintptr_t>(activeTex)),
                         imageSize,
                         ImVec2(0, 1),
                         ImVec2(1, 0));
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Wheel: zoom | Right-drag/Middle-drag: pan");
            }
                    ImGui::EndChild();
                    ImGui::PopStyleColor();
                } else {
                    ImGui::TextUnformatted("Waiting for data...");
                }

                ImGui::EndChild();
                ImGui::PopStyleColor();
                ImGui::EndTable();
            }

            ImGui::End();
        }

        ImGui::Render();
        int displayW, displayH;
        glfwGetFramebufferSize(window, &displayW, &displayH);
        glViewport(0, 0, displayW, displayH);
        glClearColor(0.08f, 0.08f, 0.08f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }

    stop.store(true, std::memory_order_relaxed);
    if (producer.joinable()) {
        producer.join();
    }

    if (textureFloat != 0) {
        glDeleteTextures(1, &textureFloat);
    }
    if (textureNorm != 0) {
        glDeleteTextures(1, &textureNorm);
    }
    if (textureRgba != 0) {
        glDeleteTextures(1, &textureRgba);
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
