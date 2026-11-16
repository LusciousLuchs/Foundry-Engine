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
- [x] Text (built-in bitmap font), textures, shaders. - [ ] fonts from files, DrawModel, camera helpers (UpdateCamera)
- [x] Shaders (LoadShaderFromMemory/SetShaderValue) and rlgl state; matrix-stack calls still missing
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

## Gunsmith3D coverage (measured)
The game calls 197 distinct raylib functions. The compat layer covers 129 of them: **93% of call sites (2,033 / 2,181)**.
(Re-measure with the script in docs/PORTING.md.) Remaining, by effort:

| Area | Calls | Notes |
|---|---|---|
| Audio: `PlaySound`, `SetSound*`, `LoadSound*`, `InitAudioDevice`, ... | ~45 | Independent of rendering; miniaudio is the obvious backend. |
| `RenderTexture` + `BeginTextureMode`/`EndTextureMode` (scope picture-in-picture) | ~5 | Needs offscreen colour+depth targets; textures must keep GL's bottom-up orientation (the game flips with a negative source height). |
| rlgl matrix stack: `rlMatrixMode`, `rlLoadIdentity`, `rlFrustum`, `rlOrtho`, `rlPush/PopMatrix`, `rlMultMatrixf`, `rlViewport` | ~20 | Custom projections (the scope). Map to the renderer's per-pass matrices. |
| Windowing: monitors, fullscreen/borderless, window state/size/position, clipboard, file drop, cursor | ~30 | Plain Win32. |
| Dynamic meshes: `UpdateMeshBuffer`, `GetRayCollisionMesh` | ~6 | |
| Images/fonts from files or memory: `LoadImage`, `LoadImageFromMemory`, `ImageResize`, `LoadFontFromMemory`, `DrawTextEx`, `MeasureTextEx`, `DrawRing` | ~10 | stb_image / a real font atlas. |
| Dear ImGui via rlImGui | UI | ImGui Vulkan backend (`imgui_impl_vulkan`) on our context. |

Done: math, input, 3D/2D primitives, `Mesh`/`Material`/`DrawMesh`, **custom shaders** (all four of the game's), textures
(`LoadTextureFromImage`, mipmaps, filter/wrap), render state (depth mask/test, culling, blend, scissor), text (built-in
bitmap font only), picking.

## Shaders
The game's GLSL (`#version 330`, loose uniforms) is translated at **build time** by `tools/shaders.py` to Vulkan GLSL,
compiled to SPIR-V and embedded in `engine/render/PortedShaders.inc`. At runtime the game's own
`LoadShaderFromMemory(vs, fs)` finds the precompiled pair by hashing the two source strings: no game-code change.
`GetShaderLocation` / `SetShaderValue(V)` / `SetShaderValueMatrix` write into a CPU copy of the shader's std140 uniform
block, which is snapshotted per draw (so changing a uniform between two `DrawMesh` calls works like OpenGL).

Adding or changing a shader:
1. Put the GLSL in `shaders/<set>/<name>.vs.glsl` / `.fs.glsl` (no `.vs` = raylib's default vertex shader), byte-identical to the
   string the game passes. `tools/shaders.py extract <game/src>` copies the game's current shaders; run it again when they change.
2. `tools/shaders.py build` regenerates `PortedShaders.inc` (needs `glslangValidator`); commit the result.
Translation rules and limits: uniforms of type float/int/vec2-4/ivec2-4/mat4 and `sampler2D texture0..N` (only `texture0`
is bound so far); array uniforms use std140 16-byte strides; `gl_FragCoord` is flipped to GL's bottom-left origin; standard
uniforms `mvp matModel matNormal matView matProjection colDiffuse` are filled automatically. A shader pair that was not
precompiled fails loudly at `LoadShaderFromMemory` with instructions.

Render-state semantics follow raylib: depth test is on only inside `BeginMode3D`, back-face culling and alpha blending are
on by default, and draws execute in call order (consecutive built-in draws with the same state merge into one command).

## Editor
See [EDITOR.md](EDITOR.md). `engine/scene` (Scene model + command interpreter, pure C++),
`engine/scene/SceneRender` (draws a Scene through the compat API), `editor/` (GUI), `tools/fscene.cpp` (CLI).
