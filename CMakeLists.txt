cmake_minimum_required(VERSION 4.3)
project(
    webgpu_sketchpad
    LANGUAGES CXX C
)

option(DEV_MODE "Set up development helper settings" ON)

add_compile_definitions(_GLFW_WAYLAND)

if (NOT EMSCRIPTEN)
    add_subdirectory(glfw)
else()
    # Emscripten has built-in support for GLFW but requires the `-sUSE_GLFW=3` link option:
    add_library(glfw INTERFACE)
    target_link_options(glfw INTERFACE -sUSE_GLFW=3)
endif()

add_subdirectory(webgpu)
add_subdirectory(glfw3webgpu)

add_executable(webgpu_sketchpad
    main.cpp
    ResourceManager.cpp
    ResourceManager.h
)

if(DEV_MODE)
    # In dev mode, we load resources from the source tree, so that when we
    # dynamically edit resources (like shaders), these are correctly
    # versionned.
    target_compile_definitions(webgpu_sketchpad PRIVATE
        RESOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/resources"
    )
else()
    # In release mode, we just load resources relatively to wherever the
    # executable is launched from, so that the binary is portable
    target_compile_definitions(webgpu_sketchpad PRIVATE
        RESOURCE_DIR="./resources"
    )
endif()

target_link_libraries(webgpu_sketchpad PRIVATE glfw webgpu glfw3webgpu)

target_copy_webgpu_binaries(webgpu_sketchpad)

set_target_properties(webgpu_sketchpad PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
    #COMPILE_WARNING_AS_ERROR ON
)

if (MSVC)
    target_compile_options(webgpu_sketchpad PRIVATE /W4)
else()
    target_compile_options(webgpu_sketchpad PRIVATE -Wall -Wextra -pedantic)
endif()

if (XCODE)
    set_target_properties(webgpu_sketchpad PROPERTIES
        XCODE_GENERATE_SCHEME ON
        XCODE_SCHEME_ENABLE_GPU_FRAME_CAPTURE_MODE "Metal"
    )
endif()

if (EMSCRIPTEN)
    set_target_properties(webgpu_sketchpad PROPERTIES SUFFIX ".html")
    target_link_options(webgpu_sketchpad PRIVATE -sASYNCIFY)
endif()
