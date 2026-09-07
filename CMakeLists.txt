cmake_minimum_required(VERSION 3.20)
project(SARProcessor LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CTest)
option(SAR_BUILD_VIEWERS "Build the OpenGL/ImGui viewers" ON)

if (MSVC)
    add_compile_options(/FS)
endif()

find_package(Eigen3 CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
if(SAR_BUILD_VIEWERS)
    find_package(glfw3 CONFIG QUIET)
    if(NOT glfw3_FOUND)
        find_package(glfw CONFIG REQUIRED)
    endif()
    find_package(glew REQUIRED)
    find_package(OpenGL REQUIRED)
    find_package(imgui CONFIG REQUIRED)
    find_package(glm CONFIG REQUIRED)
endif()
if (BUILD_TESTING)
    find_package(GTest CONFIG REQUIRED)
endif()

file(GLOB_RECURSE SAR_SOURCES CONFIGURE_DEPENDS src/*.cpp)
list(FILTER SAR_SOURCES EXCLUDE REGEX "src/visualizer_imgui/.*")
file(GLOB_RECURSE SAR_TEST_SOURCES CONFIGURE_DEPENDS tests/*.cpp)
file(GLOB_RECURSE SARTAPE2GEN_SOURCES CONFIGURE_DEPENDS SarTape2Generator/src/*.cpp)
set(SARTAPE_INGEST_MAIN "")
foreach(source_file IN LISTS SAR_SOURCES)
    if (source_file MATCHES "SarTapeIngestCli.cpp$")
        set(SARTAPE_INGEST_MAIN ${source_file})
    endif()
endforeach()
if (SARTAPE_INGEST_MAIN)
    list(REMOVE_ITEM SAR_SOURCES ${SARTAPE_INGEST_MAIN})
endif()
set(RTO_VISUALIZER_MAIN "")
foreach(source_file IN LISTS SAR_SOURCES)
    if (source_file MATCHES "RtoVisualizerCli.cpp$")
        set(RTO_VISUALIZER_MAIN ${source_file})
    endif()
endforeach()
if (RTO_VISUALIZER_MAIN)
    list(REMOVE_ITEM SAR_SOURCES ${RTO_VISUALIZER_MAIN})
endif()
set(PIPELINE_RUNNER_MAIN "")
foreach(source_file IN LISTS SAR_SOURCES)
    if (source_file MATCHES "PipelineRunnerCli.cpp$")
        set(PIPELINE_RUNNER_MAIN ${source_file})
    endif()
endforeach()
if (PIPELINE_RUNNER_MAIN)
    list(REMOVE_ITEM SAR_SOURCES ${PIPELINE_RUNNER_MAIN})
endif()
set(BACKPROJ_COMPARE_MAIN "")
foreach(source_file IN LISTS SAR_SOURCES)
    if (source_file MATCHES "BackprojCompareCli.cpp$")
        set(BACKPROJ_COMPARE_MAIN ${source_file})
    endif()
endforeach()
if (BACKPROJ_COMPARE_MAIN)
    list(REMOVE_ITEM SAR_SOURCES ${BACKPROJ_COMPARE_MAIN})
endif()
set(RPF_RTO_STREAMER_MAIN "")
foreach(source_file IN LISTS SAR_SOURCES)
    if (source_file MATCHES "RpfRtoStreamerCli.cpp$")
        set(RPF_RTO_STREAMER_MAIN ${source_file})
    endif()
endforeach()
if (RPF_RTO_STREAMER_MAIN)
    list(REMOVE_ITEM SAR_SOURCES ${RPF_RTO_STREAMER_MAIN})
endif()
set(SARTAPE2GEN_MAIN "")
foreach(source_file IN LISTS SARTAPE2GEN_SOURCES)
    if (source_file MATCHES "main.cpp$")
        set(SARTAPE2GEN_MAIN ${source_file})
    endif()
endforeach()
list(FILTER SARTAPE2GEN_SOURCES EXCLUDE REGEX "main.cpp$")

add_library(sar_core ${SAR_SOURCES})
target_include_directories(sar_core PUBLIC
    include
    ${Eigen3_INCLUDE_DIRS}
    ${nlohmann_json_INCLUDE_DIRS}
)
target_link_libraries(sar_core PUBLIC
    Eigen3::Eigen
    nlohmann_json::nlohmann_json
)

function(sar_add_imgui_cli target_name main_cpp)
    add_executable(${target_name}
        ${main_cpp}
        bindings/imgui_impl_glfw.cpp
        bindings/imgui_impl_opengl3.cpp
    )

    target_include_directories(${target_name} PRIVATE
        include
        bindings
        src/visualizer_imgui
        ${GLEW_INCLUDE_DIRS}
        ${IMGUI_INCLUDE_DIRS}
    )

    target_link_libraries(${target_name} PRIVATE sar_core OpenGL::GL glm::glm Eigen3::Eigen)

    if(TARGET CONAN_PKG::glfw)
        target_link_libraries(${target_name} PRIVATE CONAN_PKG::glfw)
    elseif(TARGET glfw::glfw)
        target_link_libraries(${target_name} PRIVATE glfw::glfw)
    elseif(TARGET glfw)
        target_link_libraries(${target_name} PRIVATE glfw)
    endif()

    if(TARGET CONAN_PKG::glew)
        target_link_libraries(${target_name} PRIVATE CONAN_PKG::glew)
    else()
        target_link_libraries(${target_name} PRIVATE GLEW::GLEW)
    endif()

    if(TARGET CONAN_PKG::imgui)
        target_link_libraries(${target_name} PRIVATE CONAN_PKG::imgui)
    else()
        target_link_libraries(${target_name} PRIVATE imgui::imgui)
    endif()
endfunction()

# ImGui visualizers.
if(SAR_BUILD_VIEWERS)
    sar_add_imgui_cli(sar_imgui_viewer_cli src/visualizer_imgui/SarImguiViewer.cpp)
    sar_add_imgui_cli(sar_imgui_rpf_viewer_cli src/visualizer_imgui/SarImguiRpfViewer.cpp)
endif()

add_library(sartape2_core ${SARTAPE2GEN_SOURCES})
target_include_directories(sartape2_core PUBLIC
    SarTape2Generator/include
    include
)
add_executable(sartape2_generator ${SARTAPE2GEN_MAIN})
target_link_libraries(sartape2_generator PRIVATE sartape2_core)
if (SARTAPE_INGEST_MAIN)
    add_executable(sartape_ingest_cli ${SARTAPE_INGEST_MAIN})
    target_link_libraries(sartape_ingest_cli PRIVATE sar_core)
endif()
if (RTO_VISUALIZER_MAIN)
    add_executable(rto_visualizer_cli ${RTO_VISUALIZER_MAIN})
    target_link_libraries(rto_visualizer_cli PRIVATE sar_core)
endif()
if (PIPELINE_RUNNER_MAIN)
    add_executable(pipeline_runner_cli ${PIPELINE_RUNNER_MAIN})
    target_link_libraries(pipeline_runner_cli PRIVATE sar_core)
endif()
if (RPF_RTO_STREAMER_MAIN)
    add_executable(rpf_rto_streamer_cli ${RPF_RTO_STREAMER_MAIN})
    target_link_libraries(rpf_rto_streamer_cli PRIVATE sar_core)
endif()
if (BACKPROJ_COMPARE_MAIN)
    add_executable(backproj_compare_cli ${BACKPROJ_COMPARE_MAIN})
    target_link_libraries(backproj_compare_cli PRIVATE sar_core)
endif()

if (BUILD_TESTING)
    enable_testing()
    add_executable(sar_core_tests ${SAR_TEST_SOURCES})
    target_link_libraries(sar_core_tests PRIVATE sar_core sartape2_core GTest::gtest_main)
    target_include_directories(sar_core_tests PRIVATE SarTape2Generator/include)
    add_test(NAME sar_core_tests COMMAND sar_core_tests)
endif()
