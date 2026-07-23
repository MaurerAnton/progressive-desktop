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
    src/core/thread_pool.cpp
    src/core/crypto/olm_account.cpp
    src/core/crypto/megolm_store.cpp
    src/core/crypto/decryptor.cpp
)
target_include_directories(progressive_core PUBLIC
    src
    ${CMAKE_CURRENT_BINARY_DIR}/generated
)
target_link_libraries(progressive_core PUBLIC
    progressive_native
    CURL::libcurl
    SQLite3::SQLite3
    simdjson::simdjson
)

# --- UI widget layer (no deps on other UI libs) ---
add_library(progressive_ui_widgets STATIC
    src/ui/shared/image_loader.cpp
    src/ui/shared/theme.cpp
    src/ui/timeline/timeline_model.cpp
    src/ui/timeline/timeline_delegate.cpp
    src/ui/timeline/timeline_handlers.cpp
    src/ui/timeline/timeline_layout.cpp
    src/ui/timeline/timeline_painter.cpp
    src/ui/room/room_store.cpp
    src/ui/room/room_data_loader.cpp
    src/ui/room_list_model.cpp
    src/ui/room_list_delegate.cpp
    src/ui/chat/chat_view.cpp
    src/ui/chat/message_edit.cpp
    src/ui/chat/emoji_picker.cpp
)
target_include_directories(progressive_ui_widgets PUBLIC src)
target_link_libraries(progressive_ui_widgets PUBLIC progressive_core Qt6::Widgets)

# --- UI dialogs layer (no deps on other UI libs) ---
add_library(progressive_ui_dialogs STATIC
    src/ui/dialogs/login_dialog.cpp
    src/ui/dialogs/image_viewer_dialog.cpp
    src/ui/dialogs/room_directory_dialog.cpp
    src/ui/dialogs/threads_dialog.cpp
    src/ui/dialogs/room_settings_dialog.cpp
    src/ui/dialogs/prefs_dialog.cpp
    src/ui/dialogs/network_log_dialog.cpp
    src/ui/profile_dialog.cpp
    src/ui/profile/room_members_dialog.cpp
    src/ui/profile/user_profile_dialog.cpp
    src/ui/notifications.cpp
)
target_include_directories(progressive_ui_dialogs PUBLIC src)
target_link_libraries(progressive_ui_dialogs PUBLIC progressive_core Qt6::Widgets)

# --- UI layout layer (depends on widgets) ---
add_library(progressive_ui_layout STATIC
    src/ui/ui_layout_builder.cpp
)
target_include_directories(progressive_ui_layout PUBLIC src)
target_link_libraries(progressive_ui_layout PUBLIC progressive_ui_widgets Qt6::Widgets)

# --- UI handlers layer (depends on widgets + dialogs) ---
add_library(progressive_ui_handlers STATIC
    src/ui/handlers/auth_handler.cpp
    src/ui/handlers/room_handler.cpp
    src/ui/handlers/thread_handler.cpp
    src/ui/handlers/room_context_menu.cpp
    src/ui/handlers/toolbar_handler.cpp
    src/ui/handlers/e2ee_init_handler.cpp
    src/ui/handlers/sync_response_handler.cpp
    src/ui/handlers/attachment_handler.cpp
    src/ui/handlers/account_switcher.cpp
    src/ui/handlers/session_bootstrap.cpp
    src/ui/handlers/slash_command_handler.cpp
)
target_include_directories(progressive_ui_handlers PUBLIC src)
target_link_libraries(progressive_ui_handlers PUBLIC
    progressive_ui_widgets
    progressive_ui_dialogs
    Qt6::Widgets
)

# --- Desktop app ---
add_executable(progressive-desktop
    src/main.cpp
    src/ui/main_window.cpp
    resources/resources.qrc
)
qt_add_resources(progressive-desktop "resources/resources.qrc")
target_link_libraries(progressive-desktop PRIVATE
    progressive_ui_handlers
    progressive_ui_layout
    progressive_ui_dialogs
    progressive_ui_widgets
    Qt6::Widgets
)
target_link_options(progressive-desktop PRIVATE -rdynamic)

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
    set_tests_properties(gui_phase4 PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    find_package(Qt6 REQUIRED COMPONENTS Test)
    add_executable(test_visual tests/test_visual.cpp)
    target_link_libraries(test_visual PRIVATE
        progressive_ui_widgets
        progressive_core
        Qt6::Widgets
        Qt6::Test
    )
    target_compile_options(test_visual PRIVATE
        -include "${CMAKE_CURRENT_SOURCE_DIR}/third_party/android_shim/progressive_compat.h")
    target_include_directories(test_visual PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    set_target_properties(test_visual PROPERTIES AUTOMOC ON)
    add_test(NAME visual COMMAND test_visual)
    set_tests_properties(visual PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    # --- Ignore test fixtures from git ---
    set_tests_properties(phase1 PROPERTIES
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
endif()

# --- Install (basic, for packaging later) ---
install(TARGETS progressive-desktop RUNTIME DESTINATION bin)
