cmake_minimum_required(VERSION 3.20)
project(SARProcessor LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if (MSVC)
    add_compile_options(/FS)
endif()

find_package(Eigen3 CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(yaml-cpp CONFIG REQUIRED)
find_package(GTest CONFIG REQUIRED)

file(GLOB_RECURSE SAR_SOURCES CONFIGURE_DEPENDS src/*.cpp)
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
    fmt::fmt
    spdlog::spdlog
    yaml-cpp::yaml-cpp
)

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

enable_testing()
add_executable(sar_core_tests ${SAR_TEST_SOURCES})
target_link_libraries(sar_core_tests PRIVATE sar_core sartape2_core GTest::gtest_main)
target_include_directories(sar_core_tests PRIVATE SarTape2Generator/include)
add_test(NAME sar_core_tests COMMAND sar_core_tests)
