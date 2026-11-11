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
