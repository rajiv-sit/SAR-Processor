cmake_minimum_required(VERSION 3.20)
project(SARProcessor LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if (MSVC)
    add_compile_options(/FS)
endif()

include(${CMAKE_BINARY_DIR}/conan_toolchain.cmake)

find_package(Eigen3 CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(yaml-cpp CONFIG REQUIRED)
find_package(GTest CONFIG REQUIRED)

file(GLOB_RECURSE SAR_SOURCES CONFIGURE_DEPENDS src/*.cpp)
file(GLOB_RECURSE SAR_TEST_SOURCES CONFIGURE_DEPENDS tests/*.cpp)

add_library(sar_core ${SAR_SOURCES})
target_include_directories(sar_core PUBLIC
    include
    ${Eigen3_INCLUDE_DIRS}
    ${nlohmann_json_INCLUDE_DIRS}
)
target_link_libraries(sar_core PUBLIC
    Eigen3::Eigen
    nlohmann_json::nlohmann_json
    fmt::fmt
    spdlog::spdlog
    yaml-cpp::yaml-cpp
)

enable_testing()
add_executable(sar_core_tests ${SAR_TEST_SOURCES})
target_link_libraries(sar_core_tests PRIVATE sar_core GTest::gtest_main)
add_test(NAME sar_core_tests COMMAND sar_core_tests)
