# progressive-desktop — Pure C++/Qt6 Matrix client for Linux desktop + PineTab 2/PinePhone.
# Sister to progressive-android-next. Reuses the progressive_native core (libolm + libcurl).
#
# Build:
#   cmake --preset pinetab2        # or: cmake --preset desktop
#   cmake --build build -j4
cmake_minimum_required(VERSION 3.22)

project(progressive-desktop
    VERSION 0.1.0
    DESCRIPTION "Pure C++/Qt6 Matrix client — sister to progressive-android-next"
    LANGUAGES C CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "" FORCE)
endif()

# --- Version header (configured) ---
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/version.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/generated/core/version.h
    @ONLY
)

# --- Options ---
option(PROGRESSIVE_MOBILE "Fullscreen frameless UI for PinePhone/PineTab 2 (auto-detected at runtime)" OFF)
option(PROGRESSIVE_LTO    "Enable LTO for progressive_native (slow link, smaller binary)" OFF)
option(PROGRESSIVE_BUILD_TESTS "Build tests" ON)

# --- Qt6 ---
find_package(Qt6 6.4 REQUIRED COMPONENTS Widgets Network)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

# --- libcurl (HTTP client with TLS + SOCKS5/Tor proxy) ---
find_package(CURL REQUIRED COMPONENTS HTTPS)

# --- progressive_native (from submodule) ---
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/progressive_native.cmake)

# --- Desktop core (shared between app + tests) ---
find_package(SQLite3 REQUIRED)
add_library(progressive_core STATIC
    src/core/http_client.cpp
    src/core/matrix_client.cpp
    src/core/session_store.cpp
    src/core/sync_engine.cpp
    src/core/fast_sync.cpp
    src/core/memory_stats.cpp
    src/core/crypto/olm_account.cpp
    src/core/crypto/megolm_store.cpp
    src/core/crypto/decryptor.cpp
    src/core/notifications.cpp
)
target_include_directories(progressive_core PUBLIC
    src
    ${CMAKE_CURRENT_BINARY_DIR}/generated
)
target_link_libraries(progressive_core PUBLIC
    progressive_native
    Qt6::Core
    Qt6::Network
    Qt6::Widgets
    CURL::libcurl
    SQLite3::SQLite3
    simdjson::simdjson
)

# --- Desktop app ---
add_executable(progressive-desktop
    src/main.cpp
    src/ui/login_dialog.cpp
    src/ui/main_window.cpp
    src/ui/room_list_model.cpp
    src/ui/room_list_delegate.cpp
    src/ui/timeline_model.cpp
    src/ui/timeline_delegate.cpp
    src/ui/image_loader.cpp
    src/ui/image_viewer_dialog.cpp
    src/ui/room_directory_dialog.cpp
    src/ui/threads_dialog.cpp
    src/ui/emoji_picker.cpp
    src/ui/room_settings_dialog.cpp
    src/ui/profile_dialog.cpp
    src/ui/prefs_dialog.cpp
    src/ui/room_members_dialog.cpp
    src/ui/user_profile_dialog.cpp
    src/ui/network_log_dialog.cpp
    src/ui/chat_view.cpp
    src/ui/room_view.cpp
    src/ui/message_edit.cpp
    src/ui/theme.cpp
    resources/resources.qrc
)
qt_add_resources(progressive-desktop "resources/resources.qrc")
target_link_libraries(progressive-desktop PRIVATE
    progressive_core
    Qt6::Widgets
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

    add_executable(test_phase1 tests/test_phase1.cpp)
    target_link_libraries(test_phase1 PRIVATE progressive_core)
    target_compile_options(test_phase1 PRIVATE
        -include "${CMAKE_CURRENT_SOURCE_DIR}/third_party/android_shim/progressive_compat.h")
    add_test(NAME phase1 COMMAND test_phase1)

    add_executable(test_phase3 tests/bench_sync_parse.cpp)
    target_link_libraries(test_phase3 PRIVATE progressive_core)
    target_compile_options(test_phase3 PRIVATE
        -include "${CMAKE_CURRENT_SOURCE_DIR}/third_party/android_shim/progressive_compat.h")
    # Not registered with ctest — needs network for --record, fixture for bench

    add_executable(test_phase4 tests/test_phase4.cpp)
    add_test(NAME phase4 COMMAND test_phase4)

    # GUI test: requires Qt (automatic via ENABLE_AUTOMOC)
    add_executable(test_gui_phase4 tests/test_gui_phase4.cpp)
    target_link_libraries(test_gui_phase4 PRIVATE Qt6::Widgets)
    set_target_properties(test_gui_phase4 PROPERTIES AUTOMOC ON)
    add_test(NAME gui_phase4 COMMAND test_gui_phase4)
endif()

# --- Ignore test fixtures from git ---
set_tests_properties(phase1 PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

# --- Install (basic, for packaging later) ---
install(TARGETS progressive-desktop RUNTIME DESTINATION bin)
