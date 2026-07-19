# cmake/progressive_native.cmake
# Builds the progressive_native static library from the
# progressive-android-experiments submodule (progressive/src/main/cpp/).
#
# On desktop we:
#   - Glob all .cpp files in src/
#   - EXCLUDE Android-only glue: jni_bridge.cpp, jni_stubs_part*.cpp, tls_bridge.cpp
#   - Provide third_party/android_shim/ on the include path so
#     #include <android/log.h> resolves to a fprintf(stderr) shim
#   - Fetch libolm 3.2.16 via FetchContent (same as upstream)
#   - Download SQLite3 amalgamation at configure time (same as upstream)
#
# Result: static lib `progressive_native` — bit-identical source to Android,
# minus the JNI callback layer (which desktop replaces with direct C++ calls).

set(PN_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/progressive-android-experiments/progressive/src/main/cpp")

# --- Collect sources, exclude Android-only files ---
file(GLOB PN_SOURCES CONFIGURE_DEPENDS "${PN_ROOT}/src/*.cpp")

# Files that require a JVM (JNIEnv*, FindClass, NewStringUTF, ...) — exclude on desktop.
# tls_bridge.cpp calls back into Java for TLS; desktop uses OpenSSL directly.
# jni_bridge.cpp + jni_stubs_part*.cpp are pure JNI entry points.
set(PN_EXCLUDE_REGEX "(jni_bridge|jni_stubs_part[0-9]+|tls_bridge)\\.cpp$")
list(FILTER PN_SOURCES EXCLUDE REGEX "${PN_EXCLUDE_REGEX}")

list(LENGTH PN_SOURCES PN_SOURCES_COUNT)
message(STATUS "progressive_native: ${PN_SOURCES_COUNT} sources after excluding JNI/TLS-bridge")

# --- Android log shim ---
# Lets #include <android/log.h> resolve on Linux desktop.
set(PN_SHIM_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/android_shim")

# --- libolm (E2EE crypto) — same version as Android side ---
include(FetchContent)
FetchContent_Declare(
    olm
    GIT_REPOSITORY https://gitlab.matrix.org/matrix-org/olm.git
    GIT_TAG        3.2.16
    PATCH_COMMAND  sed -i "s/cmake_minimum_required(VERSION 3.4)/cmake_minimum_required(VERSION 3.4...3.99)/" CMakeLists.txt
)
set(OLM_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(olm)

# --- SQLite3 amalgamation (downloaded + cached in build dir) ---
# Same recipe as upstream progressive_native CMakeLists.txt.
set(SQLITE3_DIR "${CMAKE_BINARY_DIR}/sqlite-amalgamation-3450200")
set(SQLITE3_SRC "${SQLITE3_DIR}/sqlite3.c")
if(NOT EXISTS "${SQLITE3_SRC}")
    message(STATUS "Downloading SQLite3 amalgamation from sqlite.org...")
    file(DOWNLOAD
        "https://sqlite.org/2024/sqlite-amalgamation-3450200.zip"
        "${CMAKE_BINARY_DIR}/sqlite3.zip"
        SHOW_PROGRESS
        STATUS download_status
        TIMEOUT 120
    )
    list(GET download_status 0 download_code)
    if(NOT download_code EQUAL 0)
        list(GET download_status 1 download_err)
        message(FATAL_ERROR "Failed to download SQLite: ${download_err}")
    endif()
    message(STATUS "Extracting SQLite amalgamation...")
    file(MAKE_DIRECTORY "${SQLITE3_DIR}")
    execute_process(
        COMMAND python3 -c "import zipfile,sys; zipfile.ZipFile(sys.argv[1]).extractall(sys.argv[2])"
                "${CMAKE_BINARY_DIR}/sqlite3.zip" "${CMAKE_BINARY_DIR}"
        WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
        RESULT_VARIABLE extract_result
        ERROR_VARIABLE extract_err
    )
    if(NOT extract_result EQUAL 0)
        message(FATAL_ERROR "SQLite extraction failed (${extract_result}): ${extract_err}")
    endif()
    if(NOT EXISTS "${SQLITE3_SRC}")
        message(FATAL_ERROR "SQLite extraction failed — sqlite3.c not found at ${SQLITE3_SRC}")
    endif()
    message(STATUS "SQLite3 amalgamation extracted to ${SQLITE3_DIR}")
endif()

# --- The static library ---
add_library(progressive_native STATIC ${PN_SOURCES} "${SQLITE3_SRC}")

target_include_directories(progressive_native
    PUBLIC
        "${PN_ROOT}/include"
        "${SQLITE3_DIR}"
    PRIVATE
        "${PN_SHIM_DIR}"
)

target_link_libraries(progressive_native PUBLIC olm)

target_compile_definitions(progressive_native PRIVATE PROGRESSIVE_HAS_OLM=1)

# Same compile flags as upstream (minus LTO which is opt-in via PROGRESSIVE_LTO)
target_compile_options(progressive_native PRIVATE
    -fdata-sections
    -ffunction-sections
    -fvisibility=hidden
)

set_source_files_properties(
    "${SQLITE3_SRC}"
    PROPERTIES COMPILE_FLAGS
    "-DSQLITE_THREADSAFE=1 -DSQLITE_OMIT_LOAD_EXTENSION -DSQLITE_DEFAULT_MEMSTATUS=0 -DSQLITE_DEFAULT_WAL_SYNCHRONOUS=1 -DSQLITE_LIKE_DOESNT_MATCH_BLOBS -DSQLITE_MAX_EXPR_DEPTH=0 -DSQLITE_OMIT_DECLTYPE -DSQLITE_OMIT_DEPRECATED -DSQLITE_OMIT_PROGRESS_CALLBACK -DSQLITE_OMIT_SHARED_CACHE -DSQLITE_USE_ALLOCA -Os"
)

# Dead-code stripping at link time
target_link_options(progressive_native PRIVATE
    -Wl,--gc-sections
    -Wl,--exclude-libs,ALL
)

# Optional LTO (OFF by default on ARM — 491 TUs × LTO can OOM 4GB PinePhone)
option(PROGRESSIVE_LTO "Enable LTO for progressive_native (slow link, smaller binary)" OFF)
if(PROGRESSIVE_LTO)
    target_compile_options(progressive_native PRIVATE -flto)
    target_link_options(progressive_native PRIVATE -flto)
else()
    target_compile_options(progressive_native PRIVATE -Os)
endif()
