# Builds the unmodified Gunsmith3D sources against the Foundry engine instead of raylib.
#
#   cmake -S port/gunsmith3d -B build-gunsmith \
#         -DGAME_DIR=<path to the Gunsmith3D repo> \
#         -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-gcc-toolchain.cmake \
#         -DFOUNDRY_RAYLIB_DIR=<GAME_DIR>/third_party/raylib/src      # audio (raylib's raudio.c)
#   cmake --build build-gunsmith -j
#
# The game's code is not touched. Its raylib.h / raymath.h / rlgl.h / rlImGui.h includes resolve to the shim
# headers next to this file, which forward to the compat layer. Run shaders once first when the game's GLSL changes:
#   python3 tools/shaders.py extract <GAME_DIR>/src && python3 tools/shaders.py build
cmake_minimum_required(VERSION 3.16)
project(Gunsmith3D VERSION 0.8.28 LANGUAGES C CXX)
set(GAME_DIR "" CACHE PATH "Path to the Gunsmith3D source tree")
if(NOT GAME_DIR OR NOT EXISTS ${GAME_DIR}/src/main.cpp)
  message(FATAL_ERROR "Set -DGAME_DIR=<Gunsmith3D repo>")
endif()
set(GUNSMITH_STAGE "alpha" CACHE STRING "Release stage, as in the game's own CMakeLists")
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(FOUNDRY_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../.. foundry)

# ---- generated files, exactly as the game's own CMakeLists produces them
set(GUNSMITH_VERSION ${PROJECT_VERSION})
if(GUNSMITH_STAGE)
  set(GUNSMITH_VERSION ${GUNSMITH_VERSION}-${GUNSMITH_STAGE})
endif()
set(GUNSMITH_BUILD "")
configure_file(${GAME_DIR}/src/core/version.hpp.in ${CMAKE_CURRENT_BINARY_DIR}/generated/version.hpp @ONLY)
file(READ ${GAME_DIR}/CHANGELOG.md GS_CHANGELOG HEX)
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," GS_CHANGELOG "${GS_CHANGELOG}")
configure_file(${GAME_DIR}/src/ui/changelog.inc.in ${CMAKE_CURRENT_BINARY_DIR}/generated/changelog.inc @ONLY)

file(GLOB CORE_SRC ${GAME_DIR}/src/core/*.cpp)
file(GLOB APP_SRC  ${GAME_DIR}/src/app/*.cpp)
file(GLOB UI_SRC   ${GAME_DIR}/src/ui/*.cpp)
find_package(Threads REQUIRED)
add_library(gunsmith_core STATIC ${CORE_SRC})
target_include_directories(gunsmith_core PUBLIC ${GAME_DIR}/src/core ${GAME_DIR}/third_party/json ${CMAKE_CURRENT_BINARY_DIR}/generated)
target_link_libraries(gunsmith_core PUBLIC Threads::Threads)

# raylib's API comes from the compat layer through the shim headers
add_library(gs_platform INTERFACE)
target_include_directories(gs_platform INTERFACE ${CMAKE_CURRENT_LIST_DIR}/shim)
target_link_libraries(gs_platform INTERFACE foundry_imgui foundry_compat)

add_library(gunsmith_app STATIC ${APP_SRC})
target_include_directories(gunsmith_app PUBLIC ${GAME_DIR}/src)
target_link_libraries(gunsmith_app PUBLIC gunsmith_core gs_platform comdlg32 shell32 ole32 winhttp)
add_library(gunsmith_ui STATIC ${UI_SRC})
target_link_libraries(gunsmith_ui PUBLIC gunsmith_app gs_platform)
add_executable(gunsmith3d ${GAME_DIR}/src/main.cpp)
target_link_libraries(gunsmith3d PRIVATE gunsmith_ui gunsmith_app gunsmith_core gs_platform)
target_link_options(gunsmith3d PRIVATE -Wl,--stack,16777216 -static-libstdc++ -static-libgcc -mwindows)
