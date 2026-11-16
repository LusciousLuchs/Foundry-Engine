# Architecture

## Platform policy
Windows x64 only, cross-compiled with llvm-mingw (`cmake/mingw-toolchain.cmake`).
Linux users run the .exe under Wine-GE/Proton-GE: Wine forwards Vulkan (winevulkan)
and OpenGL to the native driver, so both RHI backends work there. No native Linux
code paths. Rule: no APIs that Wine lacks (stick to Vulkan 1.3 core, plain Win32).
Consequence: OpenGL stays as a fallback backend, not a Linux requirement.

## Goals
Lightweight, fast, good lighting, reasonable physics, Vulkan primary, OpenGL supported.

## Layers
    core/      math, memory arenas, job system, logging
    platform/  window, input (Win32 or SDL3/GLFW; Windows is the only supported target)
    rhi/       abstract Device/CommandList; vulkan/ and opengl/ backends
    render/    render graph, PBR materials, clustered forward+ lights, shadows
    physics/   interface + Jolt backend (own solver optional later)
    scene/     ECS, transforms, glTF loading
    compat/    raylib-style API (InitWindow, BeginDrawing, DrawModel, ...)

## Rendering
- RHI modeled on Vulkan; Vulkan 1.3 (dynamic rendering, sync2), VMA.
- GLSL/Slang -> SPIR-V; spirv-cross for the OpenGL backend.
- Clustered forward+, PBR metallic/roughness, IBL, CSM + atlas shadows,
  GTAO, HDR tonemap, bloom. Render graph handles barriers and pass order.

## Physics
Rigid bodies, sphere/box/capsule/convex/mesh colliders, fixed timestep, sleeping.
Jolt behind `physics::World` so it can be replaced.

## Performance
Archetype ECS, work-stealing jobs, multithreaded command recording,
GPU-driven indirect draws, frame arenas, bindless textures. Profile with Tracy.

## raylib porting strategy
1. `compat/` mirrors raylib's function names and types so most game code compiles unchanged.
2. Immediate-mode draws are batched into the renderer per frame.
3. Replace compat calls with native ECS/renderer APIs where it pays off (lighting, physics).

## Roadmap
1. Window + CMake + core        <- scaffold (this commit)
2. Vulkan backend: triangle, then lit glTF model
3. Render graph, PBR, clustered lights, shadows
4. Physics integration
5. OpenGL backend
6. raylib compat layer + port the game
7. Profiling and tuning

## Porting status (Gunsmith3D)
- [x] `compat/raymath.hpp`: every raymath function Gunsmith3D uses (Vector2/3, Matrix, QuaternionTransform, Unproject), verified against the game's bundled raymath.h (`-DFOUNDRY_RAYLIB_DIR=<third_party/raylib/src>`, test `raymath_diff`).
- [x] Win32 window + input with raylib key codes (`platform/Window`). Still to do: the `IsKeyDown`/`GetMousePosition`/... free functions on top of it.
- [x] Draw calls: BeginMode3D/EndMode3D, DrawCube/Sphere/Plane/Grid/Line3D/Triangle3D, DrawMesh + Mesh/Material + GenMesh*, 2D rect/rounded rect/line/circle/triangle, input and timing (`compat/raylib.hpp`; demo: `examples/raylib_demo.cpp`)
- [ ] Text (DrawText, fonts), textures, DrawModel, camera helpers (UpdateCamera)
- [ ] Shaders (LoadShader/SetShaderValue) and rlgl usage
- [ ] Dear ImGui backend for the RHI

## Vulkan status
- [x] `rhi/vulkan/VkContext`: Vulkan 1.3 instance/device/swapchain, dynamic rendering + sync2, frame sync, resize, screenshot readback. vulkan-1.dll is loaded at runtime.
- [x] `examples/triangle`: runs under Wine with a software Vulkan driver; pixel colours verified.
- [ ] Map `VkContext` onto the abstract `rhi::Device` (buffers, textures, pipelines, VMA)
- [x] `render/Renderer`: batched forward renderer (depth buffer, sun + hemisphere ambient + 8 point lights, Blinn-Phong, ACES tone mapping, sRGB output, unlit lines, 2D overlay)
- [x] Sun shadow map (2048², orthographic fit via `SetShadowBounds`, 3x3 hardware PCF, normal-offset bias)
- [ ] Cascaded shadows, point-light shadows, PBR materials, SSAO, clustered lights beyond 8
- [ ] GPU-resident meshes (DrawMesh currently re-submits CPU vertices every frame; fine for small scenes, will not scale)
- [ ] Render graph

### Running the triangle headless (Linux dev box)
    cmake -B build-win -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-gcc-toolchain.cmake && cmake --build build-win
    VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json xvfb-run -a wine64 build-win/triangle.exe --frames 8 --screenshot out.ppm
Shaders: edit `engine/shaders/*`, then `tools/compile_shaders.sh` (needs glslang-tools).
