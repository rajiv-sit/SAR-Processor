from conan import ConanFile
from conan.tools.cmake import CMakeToolchain, CMakeDeps
try:
    from conan.tools.layout import cmake_layout
except ImportError:  # Conan < 2.0
    cmake_layout = None


class SARProcessor(ConanFile):
    name = "SARProcessor"
    version = "0.1.0"
    license = "MIT"
    url = "https://github.com/rajiv-sit/SAR-Processor"
    description = "C++23 SAR IQ-to-Image processing pipeline"
    settings = "os", "compiler", "build_type", "arch"
    exports_sources = "*"
    options = {
        "shared": [True, False],
        "fPIC": [True, False],
    }
    default_options = {
        "shared": False,
        "fPIC": True,
        "imgui/*:glfw": True,
        "imgui/*:opengl3": True,
    }

    def requirements(self):
        self.requires("eigen/3.4.0")
        self.requires("fftw/3.3.10")
        self.requires("nlohmann_json/3.11.2")
        self.requires("yaml-cpp/0.8.0")
        self.requires("fmt/10.2.1")
        self.requires("spdlog/1.12.0")
        self.requires("gtest/cci.20210126")
        self.requires("glfw/3.3.8")
        self.requires("glew/2.2.0")
        self.requires("imgui/cci.20230105+1.89.2.docking")
        self.requires("glm/0.9.9.8")
        self.requires("opengl/system")

    def config_options(self):
        if self.settings.os == "Windows":
            del self.options.fPIC

    def configure(self):
        self.options["imgui/*"].glfw = True
        self.options["imgui/*"].opengl3 = True

    def layout(self):
        if cmake_layout:
            cmake_layout(self)
        else:
            self.folders.source = "."
            self.folders.build = "build"

    def generate(self):
        tc = CMakeToolchain(self)
        tc.variables["CMAKE_CXX_STANDARD"] = "23"
        tc.generate()
        deps = CMakeDeps(self)
        deps.generate()
