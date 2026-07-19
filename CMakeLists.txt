# progressive-desktop — Pure C++/Qt6 Matrix client for Linux desktop + PineTab 2/PinePhone.
# Sister to progressive-android-next. Reuses the progressive_native core (libolm + libcurl).
#
# Build:
#   cmake --preset pinetab2        # or: cmake --preset desktop
#   cmake --build build -j4
cmake_minimum_required(VERSION 3.22)

project(progressive-desktop
    VERSION 0.0.1
    DESCRIPTION "Pure C++/Qt6 Matrix client — sister to progressive-android-next"
    LANGUAGES C CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "" FORCE)
endif()

# --- Options ---
option(PROGRESSIVE_MOBILE "Fullscreen frameless UI for PinePhone/PineTab 2 (auto-detected at runtime)" OFF)
option(PROGRESSIVE_LTO    "Enable LTO for progressive_native (slow link, smaller binary)" OFF)
option(PROGRESSIVE_BUILD_TESTS "Build tests" ON)

# --- Qt6 ---
find_package(Qt6 6.4 REQUIRED COMPONENTS Widgets Network)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

# --- progressive_native (from submodule) ---
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/progressive_native.cmake)

# --- Desktop app ---
add_executable(progressive-desktop src/main.cpp)

target_link_libraries(progressive-desktop PRIVATE
    progressive_native
    Qt6::Widgets
    Qt6::Network
)

target_compile_definitions(progressive-desktop PRIVATE
    PROGRESSIVE_DESKTOP=1
)

if(PROGRESSIVE_MOBILE)
    target_compile_definitions(progressive-desktop PRIVATE PROGRESSIVE_MOBILE=1)
endif()

# --- Tests ---
if(PROGRESSIVE_BUILD_TESTS)
    enable_testing()
    # TODO: add tests/ directory in phase 1
endif()

# --- Install (basic, for packaging later) ---
install(TARGETS progressive-desktop RUNTIME DESTINATION bin)
