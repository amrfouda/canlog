cmake_minimum_required(VERSION 3.16)
project(canlog VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(CANLOG_BUILD_TESTS "Build unit tests" ON)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# Core library: no Qt, no OS-specific code, so the future GUI and the
# SocketCAN reader can both build on top of it.
add_library(canlog STATIC
    src/dbc.cpp
    src/decoder.cpp
    src/format.cpp
    src/logreader.cpp
    src/series.cpp)
target_include_directories(canlog PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

add_executable(canlog_cli apps/canlog_cli.cpp)
target_link_libraries(canlog_cli PRIVATE canlog)

# Throughput benchmark. Run it from a Release build:
#   cmake --build build --config Release && build/Release/canlog_bench
option(CANLOG_BUILD_BENCH "Build the benchmark program" ON)
if(CANLOG_BUILD_BENCH)
    add_executable(canlog_bench bench/canlog_bench.cpp)
    target_link_libraries(canlog_bench PRIVATE canlog)
    target_compile_definitions(canlog_bench PRIVATE
        CANLOG_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
endif()

# Fuzzing. The standalone mutation fuzzer works with any compiler. The libFuzzer
# binaries need Clang; configure with sanitizers so crashes are detected:
#   cmake -S . -B build-fuzz -DCMAKE_CXX_COMPILER=clang++ -DCANLOG_BUILD_FUZZ=ON \
#         -DCMAKE_CXX_FLAGS="-fsanitize=fuzzer-no-link,address,undefined -g"
#   cmake --build build-fuzz && build-fuzz/canlog_fuzz_dbc data/ -max_total_time=60
option(CANLOG_BUILD_FUZZ "Build fuzzing targets" OFF)
if(CANLOG_BUILD_FUZZ)
    add_library(canlog_fuzz_targets STATIC fuzz/fuzz_targets.cpp)
    target_include_directories(canlog_fuzz_targets PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/fuzz)
    target_link_libraries(canlog_fuzz_targets PUBLIC canlog)

    add_executable(canlog_mutation_fuzz fuzz/mutation_fuzz.cpp)
    target_link_libraries(canlog_mutation_fuzz PRIVATE canlog_fuzz_targets)

    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        foreach(name dbc log)
            add_executable(canlog_fuzz_${name} fuzz/entry_${name}.cpp)
            target_link_libraries(canlog_fuzz_${name} PRIVATE canlog_fuzz_targets)
            target_link_options(canlog_fuzz_${name} PRIVATE -fsanitize=fuzzer)
        endforeach()
    endif()
endif()

# Qt 6 GUI. Optional: skipped automatically when Qt 6 cannot be found, so the
# library, CLI and tests still build on machines without Qt. To enable it, point
# CMake at your Qt install, e.g.
#   cmake -S . -B build -DCMAKE_PREFIX_PATH=C:/Qt/6.7.2/msvc2019_64
option(CANLOG_BUILD_GUI "Build the Qt 6 GUI if Qt 6 is available" ON)
if(CANLOG_BUILD_GUI)
    find_package(Qt6 QUIET COMPONENTS Widgets)
    if(Qt6_FOUND)
        add_executable(canlog_gui
            gui/main.cpp
            gui/MainWindow.cpp
            gui/MainWindow.hpp
            gui/FrameTableModel.cpp
            gui/FrameTableModel.hpp
            gui/PlotWidget.cpp
            gui/PlotWidget.hpp)
        set_target_properties(canlog_gui PROPERTIES AUTOMOC ON)
        target_include_directories(canlog_gui PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/gui)
        target_link_libraries(canlog_gui PRIVATE canlog Qt6::Widgets)
        message(STATUS "Qt6 found: building canlog_gui")
    else()
        message(STATUS "Qt6 not found: skipping canlog_gui (set CMAKE_PREFIX_PATH to your Qt install)")
    endif()
endif()

if(CANLOG_BUILD_TESTS)
    enable_testing()
    add_executable(canlog_tests tests/test_main.cpp)
    target_link_libraries(canlog_tests PRIVATE canlog)
    target_compile_definitions(canlog_tests PRIVATE
        CANLOG_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
    add_test(NAME canlog_tests COMMAND canlog_tests)
endif()
