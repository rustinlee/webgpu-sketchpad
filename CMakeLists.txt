cmake_minimum_required(VERSION 4.3)
project(
    webgpu_sketchpad
    LANGUAGES CXX C
)

add_executable(webgpu_sketchpad main.cpp)

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
    target_link_options(webgpu_sketchpad PRIVATE -sASYNCIFY)
endif()

add_subdirectory(webgpu)
target_link_libraries(webgpu_sketchpad PRIVATE webgpu)
target_copy_webgpu_binaries(webgpu_sketchpad)
