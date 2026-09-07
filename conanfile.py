from conan import ConanFile
from conan.tools.cmake import CMakeToolchain, CMakeDeps


class SARProcessor(ConanFile):
    name = "SARProcessor"
    version = "0.1.0"
    license = "MIT"
    url = "https://github.com/rajiv-sit/SAR-Processor"
    description = "C++23 SAR ingest, QA, and visualization tools"
    settings = "os", "compiler", "build_type", "arch"
    exports_sources = "*"
    options = {
        "shared": [True, False],
        "fPIC": [True, False],
        "with_viewers": [True, False],
    }
    default_options = {
        "shared": False,
        "fPIC": True,
        "with_viewers": True,
        "imgui/*:glfw": True,
        "imgui/*:opengl3": True,
    }

    def requirements(self):
        self.requires("eigen/3.4.0")
        self.requires("nlohmann_json/3.11.2")
        self.requires("gtest/1.15.0")
        if self.options.with_viewers:
            self.requires("glfw/3.3.8")
            self.requires("glew/2.2.0")
            self.requires("imgui/cci.20230105+1.89.2.docking")
            self.requires("glm/0.9.9.8")
            self.requires("opengl/system")

    def config_options(self):
        if self.settings.os == "Windows":
            del self.options.fPIC

    def configure(self):
        if self.options.with_viewers:
            self.options["imgui/*"].glfw = True
            self.options["imgui/*"].opengl3 = True

    def layout(self):
        self.folders.source = "."
        self.folders.build = "."
        self.folders.generators = "."

    def generate(self):
        tc = CMakeToolchain(self)
        tc.variables["CMAKE_CXX_STANDARD"] = "23"
        tc.variables["SAR_BUILD_VIEWERS"] = bool(self.options.with_viewers)
        tc.generate()
        deps = CMakeDeps(self)
        deps.generate()
