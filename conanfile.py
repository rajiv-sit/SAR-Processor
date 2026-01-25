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

    def requirements(self):
        self.requires("eigen/3.4.0")
        self.requires("fftw/3.3.10")
        self.requires("nlohmann_json/3.11.2")
        self.requires("yaml-cpp/0.8.0")
        self.requires("fmt/10.2.1")
        self.requires("spdlog/1.12.0")
        self.requires("gtest/cci.20210126")

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
