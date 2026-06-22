# CMakeLists.txt -- Ubuntu OpenGL visualizer
#
# Dependencies (install on Ubuntu with):
#   sudo apt update
#   sudo apt install build-essential cmake \
#       libglfw3-dev libassimp-dev \
#       libgl1-mesa-dev libx11-dev libxrandr-dev libxi-dev libxinerama-dev libxcursor-dev
#
# GLAD (OpenGL loader) is vendored under third_party/glad/ -- see
# visualizer/README.md for how it was generated and how to regenerate it
# if you need a different GL version/profile.
#
# Build:
#   mkdir build && cd build
#   cmake ..
#   make -j$(nproc)
#   ./body_tracking_visualizer   (run from the project ROOT, not build/,
#                                  so the relative shader/model paths resolve)
#
# Optional headless tests (no GL/GLFW/Assimp needed):
#   cmake .. -DBUILD_TESTS=ON
#   make -j$(nproc)
#   ./integration_test   # UDP receive -> record -> save -> load -> play
#   ./compare_test        # two CSV recordings loaded & played in lockstep

cmake_minimum_required(VERSION 3.16)
project(ESP32BodyTrackingVisualizer CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Declared up front so BUILD_TESTS=ON can build the headless tests even on
# a machine that doesn't have GLFW/Assimp/OpenGL dev packages installed --
# those are only required for the main visualizer target below.
option(BUILD_TESTS "Build the headless integration/compare tests" OFF)
option(BUILD_VISUALIZER "Build the main OpenGL visualizer (requires GLFW/Assimp/OpenGL)" ON)

if(BUILD_VISUALIZER)
    find_package(OpenGL REQUIRED)
    find_package(glfw3 REQUIRED)
    find_package(assimp REQUIRED)
    find_package(Threads REQUIRED)

    add_executable(body_tracking_visualizer
        visualizer/src/main.cpp
        visualizer/src/Network.cpp
        visualizer/src/MotionRecorder.cpp
        visualizer/src/Model.cpp
        visualizer/src/Shader.cpp
        visualizer/src/Renderer.cpp
        third_party/glad/src/gl.c
    )

    target_include_directories(body_tracking_visualizer PRIVATE
        visualizer/include
        third_party/glad/include
    )

    target_link_libraries(body_tracking_visualizer PRIVATE
        OpenGL::GL
        glfw
        assimp
        Threads::Threads
        ${CMAKE_DL_LIBS}
    )

    target_compile_options(body_tracking_visualizer PRIVATE -Wall -Wextra)
endif()

# ---------------------------------------------------------------------
# Optional test targets -- pure logic tests (UDP networking, recording,
# CSV round-trip, playback/comparison) with ZERO OpenGL/GLFW/Assimp
# dependency. Useful for a quick sanity check on a headless machine, or
# before touching the renderer. Off by default; enable with:
#   cmake .. -DBUILD_TESTS=ON -DBUILD_VISUALIZER=OFF
# (add -DBUILD_VISUALIZER=OFF if your machine doesn't have GLFW/Assimp
# installed and you only want the headless tests)
# ---------------------------------------------------------------------
if(BUILD_TESTS)
    find_package(Threads REQUIRED)

    add_executable(integration_test
        visualizer/test/integration_test.cpp
        visualizer/src/Network.cpp
        visualizer/src/MotionRecorder.cpp
    )
    target_include_directories(integration_test PRIVATE visualizer/include)
    target_link_libraries(integration_test PRIVATE Threads::Threads)

    add_executable(compare_test
        visualizer/test/compare_test.cpp
        visualizer/src/MotionRecorder.cpp
    )
    target_include_directories(compare_test PRIVATE visualizer/include)
    target_link_libraries(compare_test PRIVATE Threads::Threads)
endif()
