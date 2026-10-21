cmake_minimum_required(VERSION 3.22)

project(modmenu LANGUAGES C CXX)

# ---------------------------------------------------------------------------
# Platform guard
# ---------------------------------------------------------------------------
if(NOT ANDROID)
    message(FATAL_ERROR
        "This project targets Android. Configure with the NDK toolchain file:\n"
        "  -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK/build/cmake/android.toolchain.cmake\n"
        "  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-24")
endif()

if(NOT ANDROID_ABI STREQUAL "arm64-v8a")
    message(FATAL_ERROR
        "Unsupported ABI '${ANDROID_ABI}'. Only arm64-v8a is built by this project.")
endif()

string(REGEX REPLACE "^android-" "" _api_level "${ANDROID_PLATFORM}")
if(_api_level VERSION_LESS 21)
    message(FATAL_ERROR "ANDROID_PLATFORM must be at least android-21")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# ---------------------------------------------------------------------------
# Dependencies
# ---------------------------------------------------------------------------
include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

# ImGui ships no CMake project, so its sources are compiled into the target.
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.91.4
    GIT_SHALLOW    TRUE
)

# PLT hooking for eglSwapBuffers. xHook ships only Android.mk, so its core
# sources are compiled straight into the target (xh_jni.c is skipped: it would
# define a second JNI_OnLoad).
FetchContent_Declare(
    xhook
    GIT_REPOSITORY https://github.com/iqiyi/xHook.git
    GIT_TAG        v1.2.0
    GIT_SHALLOW    TRUE
)

FetchContent_MakeAvailable(imgui xhook)

set(XHOOK_DIR "${xhook_SOURCE_DIR}/libxhook/jni")

# ---------------------------------------------------------------------------
# libmodmenu.so
# ---------------------------------------------------------------------------
add_library(modmenu SHARED
    src/main.cpp
    src/menu.cpp

    ${XHOOK_DIR}/xhook.c
    ${XHOOK_DIR}/xh_core.c
    ${XHOOK_DIR}/xh_elf.c
    ${XHOOK_DIR}/xh_log.c
    ${XHOOK_DIR}/xh_util.c
    ${XHOOK_DIR}/xh_version.c

    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)

target_include_directories(modmenu PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${XHOOK_DIR}
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)

target_compile_definitions(modmenu PRIVATE
    IMGUI_IMPL_OPENGL_ES3
    IMGUI_DEFINE_MATH_OPERATORS
)

target_compile_options(modmenu PRIVATE
    -Wall
    -Wextra
    -Wno-unused-parameter
    -Wno-macro-redefined
    -fno-exceptions
    -fno-rtti
    -ffunction-sections
    -fdata-sections
)

target_link_options(modmenu PRIVATE
    -Wl,--gc-sections
    -Wl,--exclude-libs,ALL
    -Wl,--build-id=none
)

target_link_libraries(modmenu PRIVATE
    android
    log
    EGL
    GLESv3
)

# xHook is C11: it needs the PAGE_SIZE/PAGE_MASK compat header and warns
# freely, so keep its build flags separate from the C++ sources.
set_source_files_properties(
    ${XHOOK_DIR}/xhook.c
    ${XHOOK_DIR}/xh_core.c
    ${XHOOK_DIR}/xh_elf.c
    ${XHOOK_DIR}/xh_log.c
    ${XHOOK_DIR}/xh_util.c
    ${XHOOK_DIR}/xh_version.c
    PROPERTIES COMPILE_OPTIONS
        "-include;${CMAKE_CURRENT_SOURCE_DIR}/src/xhook_compat.h;-Wno-unused-parameter;-Wno-sign-compare"
)

set_target_properties(modmenu PROPERTIES
    OUTPUT_NAME           "modmenu"
    PREFIX                "lib"
    SUFFIX                ".so"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib/${ANDROID_ABI}"
)

# Strip the shipped library; the symbols the injector needs stay exported.
if(CMAKE_BUILD_TYPE STREQUAL "Release" AND CMAKE_STRIP)
    add_custom_command(TARGET modmenu POST_BUILD
        COMMAND "${CMAKE_STRIP}" --strip-unneeded "$<TARGET_FILE:modmenu>"
        COMMENT "Stripping libmodmenu.so"
        VERBATIM
    )
endif()

message(STATUS "modmenu: ABI=${ANDROID_ABI} platform=${ANDROID_PLATFORM} STL=${ANDROID_STL}")
message(STATUS "modmenu: output -> ${CMAKE_BINARY_DIR}/lib/${ANDROID_ABI}/libmodmenu.so")
