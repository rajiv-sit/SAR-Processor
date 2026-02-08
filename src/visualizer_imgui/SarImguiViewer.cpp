#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>

#include <cstdint>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "rto/MemoryMappedFile.hpp"
#include "rto/SharedFramePublisher.hpp"

namespace {

struct SharedFrameTexture {
    SharedFrameTexture() : texture(0), version_(0), width(0), height(0) {}
    ~SharedFrameTexture() { cleanup(); }

    bool map(const std::filesystem::path& path) {
        path_ = path;
        if (!mapping_.map(path_, rto::SharedFramePublisher::kSharedMemorySize)) {
            return false;
        }
        glGenTextures(1, &texture);
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindTexture(GL_TEXTURE_2D, 0);
        return true;
    }

    bool update() {
        if (!mapping_.data()) {
            return false;
        }
        if (mapping_.size() < sizeof(rto::SharedFrameHeader)) {
            return false;
        }
        auto* header = reinterpret_cast<rto::SharedFrameHeader*>(mapping_.data());
        if (header->version == version_) {
            return false;
        }
        if (header->width == 0 || header->height == 0) {
            return false;
        }
        width = static_cast<int>(header->width);
        height = static_cast<int>(header->height);
        const float* pixels = reinterpret_cast<const float*>(
            reinterpret_cast<uint8_t*>(mapping_.data()) + sizeof(rto::SharedFrameHeader));
        std::size_t pixelCount = static_cast<std::size_t>(width) * height;
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexImage2D(GL_TEXTURE_2D,
                     0,
                     GL_R32F,
                     width,
                     height,
                     0,
                     GL_RED,
                     GL_FLOAT,
                     pixels);
        glBindTexture(GL_TEXTURE_2D, 0);
        version_ = header->version;
        return true;
    }

    void cleanup() {
        if (texture != 0) {
            glDeleteTextures(1, &texture);
            texture = 0;
        }
        mapping_.unmap();
    }

    GLuint texture;
    std::uint64_t version_;
    int width;
    int height;

private:
    std::filesystem::path path_;
    rto::MemoryMappedFile mapping_;
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

} // namespace

int main(int argc, char** argv) {
    std::filesystem::path sharedFile;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--shared-file" && i + 1 < argc) {
            sharedFile = argv[++i];
        }
    }
    if (sharedFile.empty()) {
        std::cerr << "Missing --shared-file argument.\n";
        return 1;
    }

    if (!initGlfw()) {
        std::cerr << "Failed to initialize GLFW.\n";
        return 1;
    }
    GLFWwindow* window = glfwCreateWindow(1280, 720, "SAR ImGui Viewer", nullptr, nullptr);
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
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330 core");

    SharedFrameTexture frameTexture;
    if (!frameTexture.map(sharedFile)) {
        std::cerr << "Failed to map shared file: " << sharedFile << "\n";
        return 1;
    }

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        if (frameTexture.update()) {
            // updated texture
        }

        ImGui::Begin("SAR Preview");
        if (frameTexture.texture != 0 && frameTexture.width > 0 && frameTexture.height > 0) {
            ImGui::Text("Resolution %dx%d", frameTexture.width, frameTexture.height);
            ImVec2 imageSize = {static_cast<float>(frameTexture.width), static_cast<float>(frameTexture.height)};
            ImGui::Image(reinterpret_cast<void*>(static_cast<uintptr_t>(frameTexture.texture)), imageSize, ImVec2(0, 1), ImVec2(1, 0));
        } else {
            ImGui::Text("Waiting for data...");
        }
        ImGui::End();

        ImGui::Render();
        int displayW, displayH;
        glfwGetFramebufferSize(window, &displayW, &displayH);
        glViewport(0, 0, displayW, displayH);
        glClearColor(0.08f, 0.08f, 0.08f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    frameTexture.cleanup();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
