#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <imgui.h>
// Use the repo-local Dear ImGui backend bindings (under SAR-Processor/bindings).
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <mutex>
#include <nlohmann/json.hpp>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "rpf/RpfProductStreamLine.hpp"
#include "visualizer/PipelinePanels.hpp"
#include "visualizer/ScientificFrame.hpp"
#include "visualizer/ScientificScan.hpp"

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

struct FramePacket : sar::visualizer::ScientificFrame {
    std::uint64_t version = 0;
    std::uint64_t timestampNs = 0;
    std::string name;
    std::uint32_t fileIndex = 0;  // 1-based index within the current scan (0 if unknown)
    std::uint32_t fileCount = 0;  // total files in the current scan (0 if unknown)
    std::uint32_t rpfFrame = 0;   // 1-based RPF frame number used for this image (0 if unknown)
    bool scientific = false;
    std::optional<sar::visualizer::ScientificScan> scan;
    std::string error;
};

class FrameStore {
public:
 void set(FramePacket pkt, const std::atomic<bool>* stopFlag) {
     // One pending frame: pausing the consumer never skips source frames.
     while (!stopFlag->load(std::memory_order_relaxed)) {
         {
             std::lock_guard<std::mutex> lock(mu_);
             if (!hasLatest_) {
                 latest_ = std::move(pkt);
                 hasLatest_ = true;
                 return;
             }
         }
         std::this_thread::sleep_for(std::chrono::milliseconds(5));
     }
 }

 bool getIfNew(std::uint64_t lastVersion, FramePacket& out) {
     std::lock_guard<std::mutex> lock(mu_);
     if (!hasLatest_ || latest_.version == lastVersion) {
         return false;
     }
     out = std::move(latest_);
     hasLatest_ = false;
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
           "                               [--validate-inputs] [--stage stage_id]\n"
           "                               [--layout dashboard|single]\n"
           "                               [--render-check-frames N] [--render-capture image.ppm]\n"
           "--validate-inputs validates .sarscan/.sarframe inputs and emits JSON without opening a "
           "window.\n"
           "Single-process ImGui visualizer for .rpf previews, .sarframe scientific products, "
           "and .sarscan bundles.\n"
           "Scan bundles default to four simultaneous processing panels; --stage selects "
           "single-stage focus.\n";
}

std::vector<std::filesystem::path> discoverInputs(const std::filesystem::path& directory,
                                                  int maxFiles, bool includeRpf) {
    // Never discard iterator errors: inaccessible input must be visible to the operator.
    std::vector<std::filesystem::path> scans;
    std::vector<std::filesystem::path> frames;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(directory)) {
        if (!entry.is_regular_file()) continue;
        const auto extension = entry.path().extension();
        if (extension == ".sarscan")
            scans.push_back(entry.path());
        else if (extension == ".sarframe" || (includeRpf && extension == ".rpf"))
            frames.push_back(entry.path());
    }
    auto files = scans.empty() ? std::move(frames) : std::move(scans);
    std::sort(files.begin(), files.end());
    if (maxFiles > 0 && files.size() > static_cast<std::size_t>(maxFiles))
        files.resize(static_cast<std::size_t>(maxFiles));
    return files;
}

nlohmann::json frameDescription(const sar::visualizer::ScientificFrame& frame) {
    nlohmann::json entry = {{"width", frame.width},
                            {"height", frame.height},
                            {"product", frame.product},
                            {"source", frame.source},
                            {"units", frame.units},
                            {"minimum", frame.minValue},
                            {"maximum", frame.maxValue},
                            {"display_scale", frame.displayScale},
                            {"geolocation", frame.geolocation.has_value()}};
    if (frame.geolocation)
        entry["coordinate_interpolation_estimate_m"] = frame.geolocation->interpolationErrorM;
    return entry;
}

int validateScientificInputs(const std::filesystem::path& inputDir, int maxFiles) {
    nlohmann::json report = {{"valid", true}, {"frames", nlohmann::json::array()}};
    try {
        const auto files = discoverInputs(inputDir, maxFiles, false);
        if (files.empty()) throw std::runtime_error("No .sarscan or .sarframe inputs found");
        for (const auto& path : files) {
            nlohmann::json entry = {{"manifest", path.string()}};
            try {
                if (path.extension() == ".sarscan") {
                    const auto scan = sar::visualizer::loadScientificScan(path);
                    entry["label"] = scan.label;
                    entry["default_stage"] = scan.defaultStage;
                    entry["stages"] = nlohmann::json::array();
                    for (const auto& stage : scan.stages) {
                        auto item =
                            stage.frame ? frameDescription(*stage.frame) : nlohmann::json::object();
                        item["id"] = stage.id;
                        item["label"] = stage.label;
                        item["available"] = stage.frame.has_value();
                        if (!stage.frame) item["reason"] = stage.reason;
                        entry["stages"].push_back(std::move(item));
                    }
                } else {
                    entry.update(frameDescription(sar::visualizer::loadScientificFrame(path)));
                }
            } catch (const std::exception& error) {
                entry["error"] = error.what();
                report["valid"] = false;
            }
            report["frames"].push_back(std::move(entry));
        }
    } catch (const std::exception& error) {
        report["valid"] = false;
        report["error"] = error.what();
    }
    report["frame_count"] = report["frames"].size();
    std::cout << report.dump(2) << '\n';
    return report["valid"].get<bool>() ? 0 : 1;
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

static void mapToRGBA(const std::uint8_t* gray, std::size_t pixelCount, ColorMode mode,
                      std::vector<std::uint8_t>& outRgba,
                      const std::vector<float>* scientificMagnitudes = nullptr) {
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
        // Invalid geographic pixels remain transparent under every palette.
        outRgba[i * 4 + 3] =
            scientificMagnitudes && !std::isfinite((*scientificMagnitudes)[i]) ? 0 : 255;
    }
}

static bool buildPreviewFromRpf(const std::string& rpfPath, int frameNum,
                                std::uint32_t previewWidth, std::uint32_t previewHeight, Image& out,
                                float& outMin, float& outMax, std::string& err) {
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

nlohmann::json readImageFramebuffer(const ImVec4& bounds, const ImDrawData& draw,
                                    int framebufferWidth, int framebufferHeight,
                                    const std::filesystem::path& capture, int sampleLimit = 512) {
    const auto pixelX = [&](float x) { return (x - draw.DisplayPos.x) * draw.FramebufferScale.x; };
    const auto pixelY = [&](float y) { return (y - draw.DisplayPos.y) * draw.FramebufferScale.y; };
    int x = std::clamp(static_cast<int>(std::ceil(pixelX(bounds.x))), 0, framebufferWidth);
    int y = std::clamp(static_cast<int>(std::ceil(pixelY(bounds.y))), 0, framebufferHeight);
    int width = std::clamp(static_cast<int>(std::floor(pixelX(bounds.z))), x, framebufferWidth) - x;
    int height =
        std::clamp(static_cast<int>(std::floor(pixelY(bounds.w))), y, framebufferHeight) - y;
    x += std::max(0, (width - sampleLimit) / 2);
    y += std::max(0, (height - sampleLimit) / 2);
    width = std::min(width, sampleLimit);
    height = std::min(height, sampleLimit);
    if (width <= 0 || height <= 0)
        throw std::runtime_error("Image has no visible framebuffer area");
    std::vector<std::uint8_t> rgb(static_cast<std::size_t>(width) * height * 3);
    GLint previousReadBuffer = 0;
    GLint previousPackAlignment = 0;
    glGetIntegerv(GL_READ_BUFFER, &previousReadBuffer);
    glGetIntegerv(GL_PACK_ALIGNMENT, &previousPackAlignment);
    glReadBuffer(GL_BACK);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glFinish();
    glReadPixels(x, framebufferHeight - y - height, width, height, GL_RGB, GL_UNSIGNED_BYTE,
                 rgb.data());
    glPixelStorei(GL_PACK_ALIGNMENT, previousPackAlignment);
    glReadBuffer(static_cast<GLenum>(previousReadBuffer));
    const auto error = glGetError();
    std::array<std::uint64_t, 256> histogram{};
    std::uint64_t nonblack = 0;
    for (std::size_t i = 0; i < rgb.size(); i += 3) {
        const auto value = std::max({rgb[i], rgb[i + 1], rgb[i + 2]});
        ++histogram[value];
        // The image child's background is RGB(5,5,5); exclude that from evidence.
        if (value > 8) ++nonblack;
    }
    const auto bins =
        std::count_if(histogram.begin(), histogram.end(), [](auto count) { return count > 0; });
    if (!capture.empty()) {
        if (std::filesystem::exists(capture))
            throw std::runtime_error("Capture path already exists");
        std::ofstream output(capture, std::ios::binary);
        output << "P6\n" << width << ' ' << height << "\n255\n";
        for (int row = height - 1; row >= 0; --row)
            output.write(reinterpret_cast<const char*>(rgb.data() +
                                                       static_cast<std::size_t>(row) * width * 3),
                         static_cast<std::streamsize>(width) * 3);
        output.flush();
        if (!output) throw std::runtime_error("Cannot write render capture: " + capture.string());
    }
    return {{"sample_width", width},
            {"sample_height", height},
            {"nonblack_pixels", nonblack},
            {"histogram", histogram},
            {"occupied_bins", bins},
            {"gl_readback_error", error},
            {"draw_command_lists", draw.CmdListsCount},
            {"draw_vertices", draw.TotalVtxCount},
            {"image_visible", nonblack > 0 && bins > 1 && error == GL_NO_ERROR}};
}

struct PanelView {
    GLuint texture = 0;
    std::uint64_t publication = 0;
    float dynamicRangeDb = 0;
    ColorMode color = ColorMode::kGrayscale;
    bool fit = true;
    float zoom = 1;
};

struct PanelRender {
    ImVec4 imageBounds{0, 0, 0, 0};
    bool messageVisible = false;
};

void uploadPanel(PanelView& view, const sar::visualizer::ScientificFrame& frame,
                 std::uint64_t publication, float dynamicRangeDb, ColorMode color) {
    if (view.texture != 0 && view.publication == publication &&
        view.dynamicRangeDb == dynamicRangeDb && view.color == color)
        return;
    const auto gray = sar::visualizer::displayToGray(frame, dynamicRangeDb);
    std::vector<std::uint8_t> rgba;
    mapToRGBA(gray.data(), gray.size(), color, rgba, &frame.pixels);
    if (view.texture == 0) glGenTextures(1, &view.texture);
    glBindTexture(GL_TEXTURE_2D, view.texture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, static_cast<GLsizei>(frame.width),
                 static_cast<GLsizei>(frame.height), 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glGenerateMipmap(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, 0);
    view.publication = publication;
    view.dynamicRangeDb = dynamicRangeDb;
    view.color = color;
}

PanelRender drawPipelinePanel(const sar::visualizer::PipelinePanel& panel, PanelView& view,
                              ImVec2 size) {
    PanelRender result;
    ImGui::PushID(panel.id.c_str());
    ImGui::BeginChild("stage", size, true);
    ImGui::TextWrapped("%s", panel.title.c_str());
    if (!panel.frame) {
        ImGui::Separator();
        ImGui::TextWrapped("Unavailable: %s", panel.message.c_str());
        result.messageVisible = ImGui::IsItemVisible();
    } else {
        const auto& frame = *panel.frame;
        ImGui::Checkbox("Fit", &view.fit);
        ImGui::SameLine();
        ImGui::Text("%u x %u", frame.width, frame.height);
        if (!panel.message.empty()) ImGui::TextWrapped("%s", panel.message.c_str());
        if (frame.product == "range_profile_magnitude")
            ImGui::TextUnformatted("Range bin (horizontal) / pulse (vertical)");
        if (panel.id == "geocoding" && panel.geographic) {
            // These are pixel-centre coordinates of the displayed georeferenced raster.
            const auto topLeft = sar::visualizer::geolocate(frame, 0, 0);
            const auto topRight = sar::visualizer::geolocate(frame, 0, frame.width - 1);
            const auto bottomLeft = sar::visualizer::geolocate(frame, frame.height - 1, 0);
            const auto bottomRight =
                sar::visualizer::geolocate(frame, frame.height - 1, frame.width - 1);
            if (topLeft && topRight && bottomLeft && bottomRight) {
                ImGui::Text("Lat/lon TL %.5f, %.5f", topLeft->latitude, topLeft->longitude);
                ImGui::Text("TR %.5f, %.5f", topRight->latitude, topRight->longitude);
                ImGui::Text("BL %.5f, %.5f | BR %.5f, %.5f", bottomLeft->latitude,
                            bottomLeft->longitude, bottomRight->latitude, bottomRight->longitude);
            }
        }
        ImGui::Separator();
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.02f, 0.02f, 0.02f, 1));
        if (ImGui::BeginChild("pixels", ImVec2(0, 0), false,
                              ImGuiWindowFlags_HorizontalScrollbar)) {
            const auto available = ImGui::GetContentRegionAvail();
            const float fitScale =
                std::max(0.0001f, std::min(available.x / static_cast<float>(frame.width),
                                           available.y / static_cast<float>(frame.height)));
            float scale = view.fit ? fitScale : view.zoom;
            const bool hovered =
                ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
            if (hovered && (ImGui::IsMouseDragging(ImGuiMouseButton_Right) ||
                            ImGui::IsMouseDragging(ImGuiMouseButton_Middle))) {
                const auto delta = ImGui::GetIO().MouseDelta;
                ImGui::SetScrollX(ImGui::GetScrollX() - delta.x);
                ImGui::SetScrollY(ImGui::GetScrollY() - delta.y);
            }
            const auto topLeft = ImGui::GetCursorScreenPos();
            const auto mouse = ImGui::GetIO().MousePos;
            const float wheel = hovered ? ImGui::GetIO().MouseWheel : 0;
            if (wheel != 0) {
                const float oldScale = scale;
                view.zoom = std::clamp(scale * std::pow(1.2f, wheel), 0.0001f, 20.0f);
                view.fit = false;
                scale = view.zoom;
                ImGui::SetScrollX(ImGui::GetScrollX() +
                                  (mouse.x - topLeft.x) * (scale / oldScale - 1));
                ImGui::SetScrollY(ImGui::GetScrollY() +
                                  (mouse.y - topLeft.y) * (scale / oldScale - 1));
            }
            ImGui::Image(reinterpret_cast<void*>(static_cast<uintptr_t>(view.texture)),
                         ImVec2(frame.width * scale, frame.height * scale));
            const auto imageMin = ImGui::GetItemRectMin();
            const auto imageMax = ImGui::GetItemRectMax();
            const auto clipMin = ImGui::GetWindowDrawList()->GetClipRectMin();
            const auto clipMax = ImGui::GetWindowDrawList()->GetClipRectMax();
            result.imageBounds =
                ImVec4(std::max(imageMin.x, clipMin.x) + 2, std::max(imageMin.y, clipMin.y) + 2,
                       std::min(imageMax.x, clipMax.x) - 2, std::min(imageMax.y, clipMax.y) - 2);
            if (ImGui::IsItemHovered()) {
                const auto col = std::clamp(static_cast<int>((mouse.x - topLeft.x) / scale), 0,
                                            static_cast<int>(frame.width) - 1);
                const auto row = std::clamp(static_cast<int>((mouse.y - topLeft.y) / scale), 0,
                                            static_cast<int>(frame.height) - 1);
                const auto value = frame.pixels[static_cast<std::size_t>(row) * frame.width + col];
                ImGui::BeginTooltip();
                ImGui::Text("Displayed pixel (row, col): %d, %d", row, col);
                if (std::isfinite(value))
                    ImGui::Text("Value: %.8g %s", value, frame.units.c_str());
                else
                    ImGui::TextUnformatted("Value: nodata");
                if (const auto coordinate = sar::visualizer::geolocate(frame, row, col)) {
                    ImGui::Text("Latitude: %.8f  Longitude: %.8f", coordinate->latitude,
                                coordinate->longitude);
                    ImGui::Text("Coordinate interpolation estimate: %.4g m",
                                coordinate->interpolationErrorM);
                    ImGui::TextUnformatted("Absolute accuracy is not established.");
                }
                ImGui::TextUnformatted("Wheel: zoom | Right/Middle drag: pan");
                ImGui::EndTooltip();
            }
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();
    }
    ImGui::EndChild();
    ImGui::PopID();
    return result;
}

static void producerThreadFn(FrameStore* store, std::filesystem::path inputDir, int baseFrameNum,
                             bool frameFromFileIndex, int maxFiles, std::uint32_t previewWidth,
                             std::uint32_t previewHeight, std::uint32_t publishSleepMs, bool loop,
                             std::uint32_t maxTextureDimension, std::atomic<bool>* stopFlag) {
    std::vector<std::filesystem::path> files;
    std::uint64_t version = 0;
    const auto sleep = [&](std::uint32_t milliseconds) {
        const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(milliseconds);
        while (!stopFlag->load(std::memory_order_relaxed) && std::chrono::steady_clock::now() < end)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
    };
    for (;;) {
        if (stopFlag->load(std::memory_order_relaxed)) return;
        FramePacket status;
        try {
            files = discoverInputs(inputDir, maxFiles, true);
            if (files.empty())
                status.error = "No .sarscan, .sarframe or .rpf files found in " + inputDir.string();
        } catch (const std::exception& error) {
            status.error =
                "Cannot read input directory: " + inputDir.string() + "\n" + error.what();
        }
        if (!status.error.empty()) {
            status.name = "Input unavailable";
            status.version = ++version;
            status.timestampNs = nowTimestampNs();
            std::cerr << status.error << std::endl;
            store->set(std::move(status), stopFlag);
            if (!loop) return;
            sleep(250);
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

            FramePacket pkt;
            pkt.version = ++version;
            pkt.timestampNs = nowTimestampNs();
            pkt.name = stem;
            pkt.fileIndex = static_cast<std::uint32_t>(i + 1);
            pkt.fileCount = static_cast<std::uint32_t>(files.size());
            try {
                if (path.extension() == ".sarscan") {
                    pkt.scan = sar::visualizer::loadScientificScan(path);
                    pkt.name = pkt.scan->label;
                    pkt.scientific = true;
                } else if (path.extension() == ".sarframe") {
                    static_cast<sar::visualizer::ScientificFrame&>(pkt) =
                        sar::visualizer::loadScientificFrame(path);
                    if (pkt.width > maxTextureDimension || pkt.height > maxTextureDimension) {
                        throw std::runtime_error(
                            "Scientific frame exceeds this GPU's maximum texture dimension");
                    }
                    pkt.scientific = true;
                } else {
                    Image preview;
                    std::string error;
                    if (!buildPreviewFromRpf(path.string(), frameToRead, previewWidth,
                                             previewHeight, preview, pkt.minValue, pkt.maxValue,
                                             error)) {
                        throw std::runtime_error(error);
                    }
                    pkt.rpfFrame = static_cast<std::uint32_t>(frameToRead);
                    pkt.width = preview.width;
                    pkt.height = preview.height;
                    pkt.pixels = std::move(preview.pixels);
                }
            } catch (const std::exception& error) {
                pkt.name = "ERROR: " + stem;
                pkt.error = error.what();
                pkt.width = 0;
                pkt.height = 0;
                pkt.pixels.clear();
                pkt.minValue = 0;
                pkt.maxValue = 0;
                std::cerr << path.string() << ": " << pkt.error << '\n';
            }
            store->set(std::move(pkt), stopFlag);

            if (publishSleepMs > 0) {
                sleep(publishSleepMs);
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
    bool validateInputs = false;
    int renderCheckFrames = 0;
    std::filesystem::path renderCapture;
    std::string requestedStage;
    bool stageExplicit = false;
    int layoutSelection =
        -1;  // automatic: dashboard for scans, single for --stage and loose frames

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            printUsage();
            return 0;
        }
        if (arg == "--validate-inputs") {
            validateInputs = true;
            continue;
        }

        std::string value;
        if (splitFlagValue(arg, "--layout", value)) {
            if (value.empty() && i + 1 < argc) value = argv[++i];
            if (value != "dashboard" && value != "single") {
                std::cerr << "--layout requires dashboard|single\n";
                return 2;
            }
            layoutSelection = value == "dashboard" ? 1 : 0;
            continue;
        }
        if (splitFlagValue(arg, "--render-check-frames", value)) {
            if (value.empty() && i + 1 < argc) value = argv[++i];
            try {
                std::size_t used = 0;
                renderCheckFrames = std::stoi(value, &used);
                if (used != value.size() || renderCheckFrames < 1 || renderCheckFrames > 10000)
                    throw std::invalid_argument("out of range");
            } catch (const std::exception&) {
                std::cerr << "--render-check-frames requires an integer in [1,10000]\n";
                return 2;
            }
            continue;
        }
        if (splitFlagValue(arg, "--render-capture", value)) {
            if (value.empty() && i + 1 < argc) value = argv[++i];
            if (value.empty()) {
                std::cerr << "Missing --render-capture path\n";
                return 2;
            }
            renderCapture = value;
            continue;
        }
        if (splitFlagValue(arg, "--stage", value)) {
            if (value.empty() && i + 1 < argc) value = argv[++i];
            if (value.empty()) {
                std::cerr << "Missing --stage id\n";
                return 2;
            }
            requestedStage = value;
            stageExplicit = true;
            continue;
        }
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
    if (!renderCapture.empty() && renderCheckFrames == 0) {
        std::cerr << "--render-capture requires --render-check-frames\n";
        return 2;
    }
    if (!renderCapture.empty() && std::filesystem::exists(renderCapture)) {
        std::cerr << "Render capture already exists: " << renderCapture.string() << '\n';
        return 2;
    }
    if (validateInputs) {
        return validateScientificInputs(inputDir, maxFiles);
    }

    std::cerr << "Viewer startup: " << inputDir.string() << std::endl;

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
    std::cerr << "OpenGL context current" << std::endl;

    if (glewInit() != GLEW_OK) {
        std::cerr << "Failed to initialize GLEW.\n";
        return 1;
    }
    std::cerr << "OpenGL renderer: " << glGetString(GL_RENDERER) << std::endl;

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
    std::cerr << "ImGui backends initialized" << std::endl;

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
    GLint maxTextureDimension = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTextureDimension);
    std::atomic<bool> stop{false};
    std::thread producer(producerThreadFn, &store, inputDir, frameNum, frameFromFileIndex, maxFiles,
                         previewWidth, previewHeight, publishSleepMs, loop,
                         static_cast<std::uint32_t>(maxTextureDimension), &stop);

    std::uint64_t lastVer = 0;
    FramePacket latest;
    std::vector<std::uint8_t> normPixels;
    std::vector<std::uint8_t> rgbaPixels;

    std::uint64_t updates = 0;
    auto fpsWindowStart = std::chrono::steady_clock::now();
    std::uint64_t fpsWindowUpdates = 0;
    double fps = 0.0;
    bool paused = false;
    float dynamicRangeDb = 50.0f;
    float lastDynamicRangeDb = dynamicRangeDb;
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
    bool firstLoop = true;
    bool pendingRenderCheck = false;
    int completedRenderChecks = 0;
    bool renderCheckFailed = false;
    std::string previousStage;
    const sar::visualizer::ScientificFrame emptyFrame;
    std::array<PanelView, 4> panelViews{};
    const auto renderCheckStarted = std::chrono::steady_clock::now();

    while (!glfwWindowShouldClose(window)) {
        if (firstLoop) std::cerr << "Entering viewer loop" << std::endl;
        glfwPollEvents();
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        if (firstLoop) std::cerr << "First ImGui frame started" << std::endl;

        const bool updated = !paused && !(renderCheckFrames > 0 && pendingRenderCheck) &&
                             store.getIfNew(lastVer, latest);
        const bool dashboard = latest.scan.has_value() &&
                               (layoutSelection == 1 || (layoutSelection == -1 && !stageExplicit));
        const auto panels = latest.scan ? sar::visualizer::pipelinePanels(*latest.scan)
                                        : std::array<sar::visualizer::PipelinePanel, 4>{};
        std::array<PanelRender, 4> panelRenders{};
        ImVec4 dashboardBounds(0, 0, 0, 0);
        const sar::visualizer::ScientificFrame* selectedFrame = &latest;
        const sar::visualizer::ScientificScanStage* selectedStage = nullptr;
        std::string stageNotice;
        std::string effectiveStage;
        std::string displayError = latest.error;
        if (dashboard) {
            for (const auto& panel : panels) {
                if (panel.frame &&
                    (panel.frame->width > static_cast<std::uint32_t>(maxTextureDimension) ||
                     panel.frame->height > static_cast<std::uint32_t>(maxTextureDimension)))
                    displayError = panel.title + " exceeds this GPU's maximum texture dimension.";
            }
        }
        if (latest.scan) {
            if (requestedStage.empty()) requestedStage = latest.scan->defaultStage;
            for (const auto& stage : latest.scan->stages)
                if (stage.id == requestedStage) selectedStage = &stage;
            if (!selectedStage || !selectedStage->frame) {
                stageNotice =
                    selectedStage ? selectedStage->reason : "Stage not present in this scan.";
                stageNotice =
                    requestedStage + ": " + stageNotice + " Showing this scan's default stage.";
                selectedStage = &latest.scan->stages[latest.scan->defaultStageIndex];
            }
            if (dashboard) selectedStage = &latest.scan->stages[latest.scan->defaultStageIndex];
            effectiveStage = selectedStage->id;
            selectedFrame = selectedStage->frame ? &*selectedStage->frame : &emptyFrame;
        }
        if (selectedFrame->width > static_cast<std::uint32_t>(maxTextureDimension) ||
            selectedFrame->height > static_cast<std::uint32_t>(maxTextureDimension)) {
            displayError = "Selected stage exceeds this GPU's maximum texture dimension.";
            selectedFrame = &emptyFrame;
        }
        const auto& displayFrame = *selectedFrame;
        const bool stageChanged = previousStage != effectiveStage;
        const bool displayUpdated = updated || stageChanged;
        if (renderCheckFrames > 0 && updated && !displayFrame.pixels.empty())
            pendingRenderCheck = true;
        ImVec4 imageBounds(0, 0, 0, 0);

        const bool rangeChanged = latest.scientific && lastDynamicRangeDb != dynamicRangeDb;
        const bool modeChanged = (lastColorMode != colorMode);
        if (dashboard && displayError.empty()) {
            for (std::size_t index = 0; index < panels.size(); ++index)
                if (panels[index].frame)
                    uploadPanel(panelViews[index], *panels[index].frame, latest.version,
                                dynamicRangeDb, colorMode);
        }
        if ((displayUpdated || rangeChanged) && !displayFrame.pixels.empty()) {
            if (updates < 2)
                std::cerr << "Displaying frame " << latest.fileIndex << ": " << displayFrame.width
                          << 'x' << displayFrame.height << " max=" << displayFrame.maxValue
                          << std::endl;
            lastVer = latest.version;
            if (updated) {
                ++updates;
                ++fpsWindowUpdates;
            }

            // Upload raw float texture.
            glBindTexture(GL_TEXTURE_2D, textureFloat);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, static_cast<GLsizei>(displayFrame.width),
                         static_cast<GLsizei>(displayFrame.height), 0, GL_RED, GL_FLOAT,
                         displayFrame.pixels.data());
            glBindTexture(GL_TEXTURE_2D, 0);

            // Upload normalized 8-bit texture (maps [min,max] -> [0,255]).
            const std::size_t pixelCount = static_cast<std::size_t>(displayFrame.width) *
                                           static_cast<std::size_t>(displayFrame.height);
            if (latest.scientific) {
                normPixels = sar::visualizer::displayToGray(displayFrame, dynamicRangeDb);
            } else {
                normPixels.resize(pixelCount);
                const float range = displayFrame.maxValue > displayFrame.minValue
                                        ? displayFrame.maxValue - displayFrame.minValue
                                        : 1.0f;
                for (std::size_t idx = 0; idx < pixelCount; ++idx) {
                    const float n = std::clamp(
                        (displayFrame.pixels[idx] - displayFrame.minValue) / range, 0.0f, 1.0f);
                    normPixels[idx] =
                        std::isfinite(n) ? static_cast<std::uint8_t>(n * 255.0f + 0.5f) : 0;
                }
            }
            glBindTexture(GL_TEXTURE_2D, textureNorm);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, static_cast<GLsizei>(displayFrame.width),
                         static_cast<GLsizei>(displayFrame.height), 0, GL_RED, GL_UNSIGNED_BYTE,
                         normPixels.data());
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                            latest.scientific ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
                            latest.scientific ? GL_NEAREST : GL_LINEAR);
            if (latest.scientific) glGenerateMipmap(GL_TEXTURE_2D);
            glBindTexture(GL_TEXTURE_2D, 0);

            // Also prepare/upload RGBA texture for color modes (generated from normalized pixels).
            mapToRGBA(normPixels.data(), pixelCount, colorMode, rgbaPixels,
                      latest.scientific ? &displayFrame.pixels : nullptr);
            glBindTexture(GL_TEXTURE_2D, textureRgba);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, static_cast<GLsizei>(displayFrame.width),
                         static_cast<GLsizei>(displayFrame.height), 0, GL_RGBA, GL_UNSIGNED_BYTE,
                         rgbaPixels.data());
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                            latest.scientific ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
                            latest.scientific ? GL_NEAREST : GL_LINEAR);
            if (latest.scientific) glGenerateMipmap(GL_TEXTURE_2D);
            glBindTexture(GL_TEXTURE_2D, 0);

            // Update histogram.
            hist.fill(0.0f);
            for (std::uint8_t v : normPixels) {
                hist[static_cast<std::size_t>(v)] += 1.0f;
            }
            const float invN = pixelCount > 0 ? (1.0f / static_cast<float>(pixelCount)) : 0.0f;
            for (float& h : hist) h *= invN;
            histValid = true;
        } else if (modeChanged && !normPixels.empty() && displayFrame.width > 0 &&
                   displayFrame.height > 0) {
            // Recolor without waiting for a new frame.
            const std::size_t pixelCount = static_cast<std::size_t>(displayFrame.width) *
                                           static_cast<std::size_t>(displayFrame.height);
            mapToRGBA(normPixels.data(), pixelCount, colorMode, rgbaPixels,
                      latest.scientific ? &displayFrame.pixels : nullptr);
            glBindTexture(GL_TEXTURE_2D, textureRgba);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, static_cast<GLsizei>(displayFrame.width),
                         static_cast<GLsizei>(displayFrame.height), 0, GL_RGBA, GL_UNSIGNED_BYTE,
                         rgbaPixels.data());
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                            latest.scientific ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
                            latest.scientific ? GL_NEAREST : GL_LINEAR);
            if (latest.scientific) glGenerateMipmap(GL_TEXTURE_2D);
            glBindTexture(GL_TEXTURE_2D, 0);
        }
        if (displayFrame.pixels.empty()) {
            histValid = false;
            normPixels.clear();
        }
        if (updated) lastVer = latest.version;
        previousStage = effectiveStage;
        lastColorMode = colorMode;
        lastDynamicRangeDb = dynamicRangeDb;

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

            if (ImGui::BeginTable(
                    "layout", 2, ImGuiTableFlags_NoSavedSettings | ImGuiTableFlags_BordersInnerV)) {
                ImGui::TableSetupColumn("Display", ImGuiTableColumnFlags_WidthFixed,
                                        std::min(300.0f, ImGui::GetContentRegionAvail().x * 0.30f));
                ImGui::TableSetupColumn("Image", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableNextRow();

                // Left panel: display + controls + histogram.
                ImGui::TableSetColumnIndex(0);
                ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.03f, 0.07f, 0.13f, 1.00f)); // dark blue
                ImGui::BeginChild("display_panel", ImVec2(0, 0), false);
                ImGui::TextUnformatted("Display");
                ImGui::Separator();

                ImGui::TextWrapped("Input: %s", inputDir.string().c_str());
                if (latest.fileIndex > 0 && latest.fileCount > 0) {
                    ImGui::Text("File: %u / %u", latest.fileIndex, latest.fileCount);
                } else {
                    ImGui::TextUnformatted("File: n/a");
                }
                ImGui::TextWrapped("Name: %s", latest.name.empty() ? "n/a" : latest.name.c_str());
                if (ImGui::Button(paused ? "Resume playback" : "Pause to inspect"))
                    paused = !paused;
                if (latest.scan) {
                    if (ImGui::Button(dashboard ? "Focus one stage" : "Show all four stages"))
                        layoutSelection = dashboard ? 0 : 1;
                    if (!dashboard) {
                        if (ImGui::BeginCombo("Processing stage", selectedStage
                                                                      ? selectedStage->label.c_str()
                                                                      : "n/a")) {
                            for (const auto& stage : latest.scan->stages) {
                                ImGui::BeginDisabled(!stage.frame.has_value());
                                if (ImGui::Selectable(stage.label.c_str(),
                                                      stage.id == effectiveStage)) {
                                    requestedStage = stage.id;
                                    paused = true;
                                    layoutSelection = 0;
                                }
                                ImGui::EndDisabled();
                                if (!stage.frame &&
                                    ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                                    ImGui::SetTooltip("%s", stage.reason.c_str());
                            }
                            ImGui::EndCombo();
                        }
                        if (!stageNotice.empty()) ImGui::TextWrapped("%s", stageNotice.c_str());
                    }
                    ImGui::TextWrapped("Scan source: %s", latest.scan->source.c_str());
                    if (!dashboard)
                        for (const auto& stage : latest.scan->stages) {
                            if (!stage.frame)
                                ImGui::TextWrapped("%s: %s", stage.label.c_str(),
                                                   stage.reason.c_str());
                        }
                }
                if (!displayError.empty())
                    ImGui::TextWrapped("Load error: %s", displayError.c_str());
                if (latest.scientific && !dashboard) {
                    ImGui::TextWrapped("Product: %s", displayFrame.product.c_str());
                    ImGui::TextWrapped("Source: %s", displayFrame.source.c_str());
                    ImGui::TextWrapped("Units: %s", displayFrame.units.c_str());
                    if (displayFrame.geolocation) {
                        ImGui::TextWrapped(
                            "Coordinates interpolated from source georeferencing. Absolute "
                            "accuracy is not established.");
                        ImGui::Text("Interpolation estimate: %.4g m",
                                    displayFrame.geolocation->interpolationErrorM);
                    } else {
                        ImGui::TextWrapped("Scene-local image: no Earth coordinates available.");
                    }
                }
                if (!latest.scientific)
                    ImGui::Text("RPF frame arg: %d  Mode: %s", frameNum,
                                frameFromFileIndex ? "file-index" : "fixed");
                if (latest.rpfFrame > 0) ImGui::Text("RPF frame used: %u", latest.rpfFrame);

                if (showStats) {
                    ImGui::Separator();
                    ImGui::Text("Resolution: %ux%u", displayFrame.width, displayFrame.height);
                    ImGui::Text("Min/Max: %.6g / %.6g", displayFrame.minValue,
                                displayFrame.maxValue);
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
                if (!dashboard && latest.scientific &&
                    displayFrame.displayScale == "phase_radians") {
                    ImGui::TextUnformatted("Phase: -pi to +pi radians");
                } else if (latest.scientific) {
                    ImGui::SliderFloat("Dynamic range (dB)", &dynamicRangeDb, 10.0f, 100.0f,
                                       "%.0f dB");
                    ImGui::TextUnformatted("Magnitude: 20 log10(value / peak)");
                } else {
                    ImGui::Checkbox("Show normalized (recommended)", &showNormalized);
                }

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

                if (dashboard) {
                    if (ImGui::Button("Fit all panels"))
                        for (auto& view : panelViews) view.fit = true;
                    ImGui::TextWrapped("Each panel: wheel zoom, right/middle drag to pan.");
                } else {
                    ImGui::Checkbox("Fit to window", &fitToWindow);
                    if (!fitToWindow) {
                        ImGui::SliderFloat("Zoom", &zoom, zoomMin, zoomMax, "%.2fx",
                                           ImGuiSliderFlags_Logarithmic);
                    } else {
                        ImGui::TextUnformatted("Wheel zoom disables fit mode.");
                    }
                }

                ImGui::Separator();
                ImGui::TextUnformatted(dashboard ? "Final image histogram" : "Histogram");
                if (histValid) {
                    ImGui::PlotHistogram("##hist", hist.data(), static_cast<int>(hist.size()), 0,
                                         nullptr, 0.0f, 0.03f,
                                         ImVec2(-1, dashboard ? 60.0f : 120.0f));
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
                if (dashboard && displayError.empty()) {
                    const auto topLeft = ImGui::GetCursorScreenPos();
                    const auto available = ImGui::GetContentRegionAvail();
                    dashboardBounds = ImVec4(topLeft.x, topLeft.y, topLeft.x + available.x,
                                             topLeft.y + available.y);
                    const auto spacing = ImGui::GetStyle().ItemSpacing;
                    const ImVec2 cell(std::max(1.0f, (available.x - spacing.x) * 0.5f),
                                      std::max(1.0f, (available.y - spacing.y) * 0.5f));
                    for (std::size_t index = 0; index < panels.size(); ++index) {
                        if (index % 2 != 0) ImGui::SameLine();
                        panelRenders[index] =
                            drawPipelinePanel(panels[index], panelViews[index], cell);
                    }
                } else {
                    ImGui::TextUnformatted(selectedStage ? selectedStage->label.c_str() : "Image");
                    if (displayFrame.product == "range_profile_magnitude")
                        ImGui::TextUnformatted("Horizontal: range bin  |  Vertical: pulse");
                    ImGui::Separator();

                    const GLuint activeTex =
                        (latest.scientific || showNormalized)
                            ? (colorMode == ColorMode::kGrayscale ? textureNorm : textureRgba)
                            : textureFloat;

                    if (activeTex != 0 && displayFrame.width > 0 && displayFrame.height > 0 &&
                        !displayFrame.pixels.empty()) {
                        ImVec2 avail = ImGui::GetContentRegionAvail();
                        const ImVec2 childSize = ImVec2(avail.x, std::max(200.0f, avail.y));
                        // Image viewport itself: neutral near-black, separate from blue chrome.
                        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.02f, 0.02f, 0.02f, 1.00f));
                        ImGui::BeginChild("image_view", childSize, true,
                                          ImGuiWindowFlags_HorizontalScrollbar);

                        auto computeFitScale = [&]() -> float {
                            const ImVec2 inner = ImGui::GetContentRegionAvail();
                            const float sx = inner.x / static_cast<float>(displayFrame.width);
                            const float sy = inner.y / static_cast<float>(displayFrame.height);
                            return std::max(zoomMin, std::min(sx, sy));
                        };

                        float scale = fitToWindow ? computeFitScale() : zoom;
                        if (!fitToWindow) {
                            scale = std::clamp(scale, zoomMin, zoomMax);
                        }

                        const bool hovered =
                            ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
                        const float wheel = hovered ? ImGui::GetIO().MouseWheel : 0.0f;

                        if (hovered && (ImGui::IsMouseDragging(ImGuiMouseButton_Right) ||
                                        ImGui::IsMouseDragging(ImGuiMouseButton_Middle))) {
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
                            // imageTopLeft already includes the current scroll offset.
                            // Preserve the source pixel under the cursor when zooming after a pan.
                            const float scaleDelta = scale / oldScale - 1.0f;
                            ImGui::SetScrollX(ImGui::GetScrollX() + viewX * scaleDelta);
                            ImGui::SetScrollY(ImGui::GetScrollY() + viewY * scaleDelta);
                        }

                        const ImVec2 imageSize = {static_cast<float>(displayFrame.width) * scale,
                                                  static_cast<float>(displayFrame.height) * scale};
                        ImGui::Image(reinterpret_cast<void*>(static_cast<uintptr_t>(activeTex)),
                                     imageSize, latest.scientific ? ImVec2(0, 0) : ImVec2(0, 1),
                                     latest.scientific ? ImVec2(1, 1) : ImVec2(1, 0));
                        const ImVec2 imageMin = ImGui::GetItemRectMin();
                        const ImVec2 imageMax = ImGui::GetItemRectMax();
                        const ImVec2 clipMin = ImGui::GetWindowDrawList()->GetClipRectMin();
                        const ImVec2 clipMax = ImGui::GetWindowDrawList()->GetClipRectMax();
                        imageBounds = ImVec4(std::max(imageMin.x, clipMin.x) + 2,
                                             std::max(imageMin.y, clipMin.y) + 2,
                                             std::min(imageMax.x, clipMax.x) - 2,
                                             std::min(imageMax.y, clipMax.y) - 2);
                        if (ImGui::IsItemHovered()) {
                            ImGui::BeginTooltip();
                            if (latest.scientific) {
                                const ImVec2 mouse = ImGui::GetIO().MousePos;
                                const auto col =
                                    std::clamp(static_cast<int>((mouse.x - imageTopLeft.x) / scale),
                                               0, static_cast<int>(displayFrame.width) - 1);
                                const auto row =
                                    std::clamp(static_cast<int>((mouse.y - imageTopLeft.y) / scale),
                                               0, static_cast<int>(displayFrame.height) - 1);
                                const float pixel =
                                    displayFrame
                                        .pixels[static_cast<std::size_t>(row) * displayFrame.width +
                                                col];
                                ImGui::Text("Displayed pixel (row, col): %d, %d", row, col);
                                if (std::isfinite(pixel))
                                    ImGui::Text("Value: %.8g %s", pixel,
                                                displayFrame.units.c_str());
                                else
                                    ImGui::TextUnformatted("Value: nodata");
                                const auto coordinate =
                                    sar::visualizer::geolocate(displayFrame, row, col);
                                if (coordinate) {
                                    ImGui::Text("Latitude: %.8f  Longitude: %.8f",
                                                coordinate->latitude, coordinate->longitude);
                                    ImGui::Text("Coordinate interpolation estimate: %.4g m",
                                                coordinate->interpolationErrorM);
                                }
                            }
                            ImGui::TextUnformatted("Wheel: zoom | Right/Middle drag: pan");
                            ImGui::EndTooltip();
                        }
                        ImGui::EndChild();
                        ImGui::PopStyleColor();
                    } else {
                        ImGui::TextWrapped("%s", displayError.empty()
                                                     ? "Waiting for a readable image..."
                                                     : displayError.c_str());
                    }
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
        const auto drawError = glGetError();
        if (firstLoop)
            std::cerr << "First render: framebuffer=" << displayW << 'x' << displayH
                      << " vertices=" << ImGui::GetDrawData()->TotalVtxCount
                      << " GL error=" << drawError << std::endl;
        bool dashboardReady = dashboard && displayError.empty();
        if (dashboardReady) {
            for (std::size_t index = 0; index < panels.size(); ++index) {
                const auto& bounds = panelRenders[index].imageBounds;
                dashboardReady &= panels[index].frame ? bounds.z > bounds.x && bounds.w > bounds.y
                                                      : panelRenders[index].messageVisible;
            }
        }
        const bool imageReady =
            dashboard ? dashboardReady
                      : imageBounds.z > imageBounds.x && imageBounds.w > imageBounds.y;
        if (renderCheckFrames > 0 && pendingRenderCheck && imageReady &&
            ImGui::GetDrawData()->TotalVtxCount > 0) {
            try {
                const auto renderError = drawError;
                nlohmann::json report;
                if (dashboard) {
                    report = {{"layout", "dashboard"},
                              {"panels", nlohmann::json::array()},
                              {"image_visible", true}};
                    for (std::size_t index = 0; index < panels.size(); ++index) {
                        const auto& panel = panels[index];
                        nlohmann::json item = {{"id", panel.id},
                                               {"title", panel.title},
                                               {"available", panel.frame != nullptr},
                                               {"reason", panel.message}};
                        if (panel.frame) {
                            item.update(readImageFramebuffer(panelRenders[index].imageBounds,
                                                             *ImGui::GetDrawData(), displayW,
                                                             displayH, {}));
                            item["passed"] = item["image_visible"].get<bool>();
                            item["frame_width"] = panel.frame->width;
                            item["frame_height"] = panel.frame->height;
                            item["product"] = panel.frame->product;
                            item["source"] = panel.frame->source;
                            item["geographic"] = panel.geographic;
                        } else {
                            item["message_visible"] = panelRenders[index].messageVisible;
                            item["passed"] =
                                panelRenders[index].messageVisible && !panel.message.empty();
                        }
                        report["image_visible"] =
                            report["image_visible"].get<bool>() && item["passed"].get<bool>();
                        report["panels"].push_back(std::move(item));
                    }
                    if (completedRenderChecks == 0 && !renderCapture.empty() &&
                        report["image_visible"].get<bool>() && renderError == GL_NO_ERROR) {
                        const auto capture = readImageFramebuffer(
                            dashboardBounds, *ImGui::GetDrawData(), displayW, displayH,
                            renderCapture, std::max(displayW, displayH));
                        if (capture["gl_readback_error"].get<unsigned>() != GL_NO_ERROR)
                            throw std::runtime_error("Dashboard capture GL readback failed");
                    }
                } else {
                    report = readImageFramebuffer(
                        imageBounds, *ImGui::GetDrawData(), displayW, displayH,
                        completedRenderChecks == 0 ? renderCapture : std::filesystem::path{});
                    report["layout"] = "single";
                }
                report["event"] = "render_check";
                report["file_index"] = latest.fileIndex;
                report["file_count"] = latest.fileCount;
                report["publication"] = latest.version;
                report["frame_width"] = displayFrame.width;
                report["frame_height"] = displayFrame.height;
                report["name"] = latest.name;
                report["stage"] = effectiveStage;
                report["requested_stage"] = requestedStage;
                report["product"] = displayFrame.product;
                report["display_scale"] = displayFrame.displayScale;
                report["gl_render_error"] = renderError;
                report["passed"] =
                    report["image_visible"].get<bool>() && renderError == GL_NO_ERROR;
                if (!report["passed"].get<bool>()) renderCheckFailed = true;
                std::cout << report.dump() << std::endl;
            } catch (const std::exception& error) {
                std::cout << nlohmann::json({{"event", "render_check"},
                                             {"passed", false},
                                             {"error", error.what()}})
                                 .dump()
                          << std::endl;
                renderCheckFailed = true;
            }
            pendingRenderCheck = false;
            ++completedRenderChecks;
            if (completedRenderChecks >= renderCheckFrames)
                glfwSetWindowShouldClose(window, GLFW_TRUE);
        }
        if (renderCheckFrames > 0 &&
            ((!displayError.empty() && updated) ||
             std::chrono::steady_clock::now() - renderCheckStarted >
                 std::chrono::seconds(std::max(60, renderCheckFrames * 10)))) {
            std::cout << nlohmann::json({{"event", "render_check"},
                                         {"passed", false},
                                         {"error", displayError.empty()
                                                       ? "Timed out waiting for rendered images"
                                                       : displayError}})
                             .dump()
                      << std::endl;
            renderCheckFailed = true;
            glfwSetWindowShouldClose(window, GLFW_TRUE);
        }
        glfwSwapBuffers(window);
        firstLoop = false;
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
    for (auto& view : panelViews)
        if (view.texture != 0) glDeleteTextures(1, &view.texture);

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();
    return renderCheckFrames > 0 && (renderCheckFailed || completedRenderChecks < renderCheckFrames)
               ? 1
               : 0;
}
