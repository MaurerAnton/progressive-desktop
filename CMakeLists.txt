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
# libsodium: ed25519 keypair generation + curve25519/AEAD for cross-signing
# and key backup (Phases 6-7). PineTab/Arch: pacman -S libsodium. CI: apt install libsodium-dev.
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(SODIUM QUIET IMPORTED_TARGET libsodium)
endif()
if(NOT TARGET PkgConfig::SODIUM)
    find_library(SODIUM_LIBRARY sodium REQUIRED)
    find_path(SODIUM_INCLUDE_DIR sodium.h REQUIRED)
    add_library(PkgConfig::SODIUM UNKNOWN IMPORTED)
    set_target_properties(PkgConfig::SODIUM PROPERTIES
        IMPORTED_LOCATION "${SODIUM_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${SODIUM_INCLUDE_DIR}")
endif()
add_library(progressive_core STATIC
    src/core/http_client.cpp
    src/core/json_utils.cpp
    src/core/matrix_client.cpp
    src/core/session_store.cpp
    src/core/sync_engine.cpp
    src/core/fast_sync.cpp
    src/core/memory_stats.cpp
    src/core/thread_pool.cpp
    src/core/crypto/ed25519.cpp
    src/core/crypto/olm_account.cpp
    src/core/crypto/random.cpp
    src/core/crypto/sig_verify.cpp
    src/core/crypto/sas.cpp
    src/core/crypto/sas_emojis.cpp
    src/core/crypto/verification.cpp
    src/core/crypto/verify_controller.cpp
    src/core/crypto/megolm_store.cpp
    src/core/crypto/cross_sign.cpp
    src/core/crypto/recovery_key.cpp
    src/core/crypto/backup_crypto.cpp
    src/core/crypto/decryptor.cpp
)
target_include_directories(progressive_core PUBLIC
    src
    ${CMAKE_CURRENT_BINARY_DIR}/generated
)
target_link_libraries(progressive_core PUBLIC
    progressive_native
    CURL::libcurl
    SQLite::SQLite3
    simdjson::simdjson
    PkgConfig::SODIUM
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
    src/ui/room/event_parser.cpp
    src/ui/room_list_model.cpp
    src/ui/room_list_delegate.cpp
    src/ui/chat/chat_logger.cpp
    src/ui/chat/chat_view.cpp
    src/ui/chat/message_edit.cpp
    src/ui/chat/emoji_picker.cpp
)
target_include_directories(progressive_ui_widgets PUBLIC src)
target_link_libraries(progressive_ui_widgets PUBLIC progressive_core progressive_ui_dialogs Qt6::Widgets)
# --- UI dialogs layer (no deps on other UI libs) ---
add_library(progressive_ui_dialogs STATIC
    src/ui/dialogs/login_dialog.cpp
    src/ui/dialogs/color_settings_dialog.cpp
    src/ui/dialogs/image_viewer_dialog.cpp
    src/ui/dialogs/room_directory_dialog.cpp
    src/ui/dialogs/room_settings_dialog.cpp
    src/ui/dialogs/room_switcher_dialog.cpp
    src/ui/dialogs/threads_dialog.cpp
    src/ui/dialogs/shortcuts_dialog.cpp
    src/ui/dialogs/sas_verification_dialog.cpp
    src/ui/dialogs/prefs_dialog.cpp
    src/ui/dialogs/network_log_dialog.cpp
    src/ui/profile_dialog.cpp
    src/ui/profile/room_members_dialog.cpp
    src/ui/profile/user_profile_dialog.cpp
    src/ui/notifications.cpp
)
target_include_directories(progressive_ui_dialogs PUBLIC src)
target_link_libraries(progressive_ui_dialogs PUBLIC progressive_core progressive_ui_widgets Qt6::Widgets)

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
    src/ui/handlers/room_key_helper.cpp
    src/ui/handlers/toolbar_handler.cpp
    src/ui/handlers/e2ee_init_handler.cpp
    src/ui/handlers/sync_response_handler.cpp
    src/ui/handlers/attachment_handler.cpp
    src/ui/handlers/account_switcher.cpp
    src/ui/handlers/session_bootstrap.cpp
    src/ui/handlers/slash_command_handler.cpp
    src/ui/handlers/verification_handler.cpp
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

    add_executable(test_olm_inbound tests/test_olm_inbound.cpp)
    target_link_libraries(test_olm_inbound PRIVATE progressive_core)
    target_compile_options(test_olm_inbound PRIVATE
        -include "${CMAKE_CURRENT_SOURCE_DIR}/third_party/android_shim/progressive_compat.h")
    add_test(NAME olm_inbound COMMAND test_olm_inbound)

    add_executable(test_megolm_inbound tests/test_megolm_inbound.cpp)
    target_link_libraries(test_megolm_inbound PRIVATE progressive_core)
    target_compile_options(test_megolm_inbound PRIVATE
        -include "${CMAKE_CURRENT_SOURCE_DIR}/third_party/android_shim/progressive_compat.h")
    add_test(NAME megolm_inbound COMMAND test_megolm_inbound)

    add_executable(test_e2ee_account tests/test_e2ee_account.cpp)
    target_link_libraries(test_e2ee_account PRIVATE progressive_core)
    target_compile_options(test_e2ee_account PRIVATE
        -include "${CMAKE_CURRENT_SOURCE_DIR}/third_party/android_shim/progressive_compat.h")
    add_test(NAME e2ee_account COMMAND test_e2ee_account)

    add_executable(test_e2ee_otk_count tests/test_e2ee_otk_count.cpp)
    target_link_libraries(test_e2ee_otk_count PRIVATE progressive_core)
    target_compile_options(test_e2ee_otk_count PRIVATE
        -include "${CMAKE_CURRENT_SOURCE_DIR}/third_party/android_shim/progressive_compat.h")
    add_test(NAME e2ee_otk_count COMMAND test_e2ee_otk_count)

    add_executable(test_e2ee_sas tests/test_e2ee_sas.cpp)
    target_link_libraries(test_e2ee_sas PRIVATE progressive_core)
    target_compile_options(test_e2ee_sas PRIVATE
        -include "${CMAKE_CURRENT_SOURCE_DIR}/third_party/android_shim/progressive_compat.h")
    add_test(NAME e2ee_sas COMMAND test_e2ee_sas)

    add_executable(test_e2ee_verify_protocol tests/test_e2ee_verify_protocol.cpp)
    target_link_libraries(test_e2ee_verify_protocol PRIVATE progressive_core)
    target_compile_options(test_e2ee_verify_protocol PRIVATE
        -include "${CMAKE_CURRENT_SOURCE_DIR}/third_party/android_shim/progressive_compat.h")
    add_test(NAME e2ee_verify_protocol COMMAND test_e2ee_verify_protocol)

    add_executable(test_e2ee_store tests/test_e2ee_store.cpp)
    target_link_libraries(test_e2ee_store PRIVATE progressive_core)
    target_compile_options(test_e2ee_store PRIVATE
        -include "${CMAKE_CURRENT_SOURCE_DIR}/third_party/android_shim/progressive_compat.h")
    add_test(NAME e2ee_store COMMAND test_e2ee_store)

    # Live-Synapse E2EE integration test (CI: synapse-e2ee workflow).
    # Skips gracefully when no server is reachable (no SYNAPSE_URL set).
    add_executable(test_synapse_e2ee tests/test_synapse_e2ee.cpp)
    target_link_libraries(test_synapse_e2ee PRIVATE progressive_core)
    target_compile_options(test_synapse_e2ee PRIVATE
        -include "${CMAKE_CURRENT_SOURCE_DIR}/third_party/android_shim/progressive_compat.h")
    add_test(NAME synapse_e2ee COMMAND test_synapse_e2ee)
    set_tests_properties(synapse_e2ee PROPERTIES TIMEOUT 120)

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
        progressive_ui_handlers
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
