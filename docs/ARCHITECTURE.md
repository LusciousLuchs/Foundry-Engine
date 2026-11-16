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
- [x] GPU-resident meshes for custom-shader draws (`UploadMesh`/`UnloadMesh`/`UpdateMeshBuffer`; static = device-local, dynamic = per-frame-slot copies). The built-in lit path (editor, demos) still transforms vertices on the CPU.
- [ ] Render graph

### Running the triangle headless (Linux dev box)
    cmake -B build-win -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-gcc-toolchain.cmake && cmake --build build-win
    VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json xvfb-run -a wine64 build-win/triangle.exe --frames 8 --screenshot out.ppm
Shaders: edit `engine/shaders/*`, then `tools/compile_shaders.sh` (needs glslang-tools).

## Gunsmith3D coverage (measured)
Compile probe against the game's own sources (`-fsyntax-only`, raylib replaced by the compat layer through shim headers): the
~400 initial errors are down to the game's generated-file stubs only. The game is also built end to end out of tree; see
docs/PORTING.md for how to repeat both.

Implemented: math, input (incl. keypad, key queue, typed characters), timing and frame limiter, 3D/2D primitives,
`Mesh`/`Material`/`DrawMesh`/GPU-resident meshes/`UpdateMeshBuffer`, custom shaders, textures/images (PNG/JPEG/BMP/TGA via
stb_image, bilinear resize, generated images), fonts (stb_truetype atlas, UTF-8, `DrawTextEx`/`MeasureTextEx`) plus the built-in
bitmap font, render textures (`BeginTextureMode`, GL-style orientation so the game's negative-height flip works), render state
(depth mask/test, culling, blend, scissor, viewport), the rlgl matrix stack (`rlMatrixMode/LoadIdentity/Frustum/Ortho/Push/Pop/
MultMatrixf`, `rlViewport`), picking (`GetRayCollisionTriangle/Mesh`, screen<->world), window state (fullscreen/borderless as a
borderless window, size/position/min size, topmost, resizable, vsync via present mode, monitors, clipboard, file drop),
`DecompressData`, and audio (raylib's own `raudio.c` compiled from the raylib source directory).

Known differences from raylib:
- Fullscreen is a borderless window over the monitor, not an exclusive video mode.
- `BeginTextureMode` must be called between `BeginDrawing` and `EndDrawing` (the renderer records per frame).
- `UpdateMeshBuffer` works on dynamic, non-indexed meshes (what the game uses); static meshes are immutable.
- Only `texture0` is bound for custom shaders. `FLAG_MSAA_4X_HINT` gives 4x MSAA on the window pass (render targets are single-sampled).
- Audio needs `-DFOUNDRY_RAYLIB_DIR=<raylib/src>` (the engine builds `raudio.c` from there); sound output itself is untested here.

## Measured performance (CPU cost of submitting a draw; GPU time not included)
`examples/bench_drawmesh.cpp`, Release build, per `DrawMesh` call:

| Mesh | GPU-resident (custom shader) | Re-sent each draw | Built-in lit (CPU transform) |
|---|---|---|---|
| 256 tris | 0.2 µs | 6.6 µs | 8.8 µs |
| 2,048 tris | 0.3 µs | 99 µs | 69 µs |
| 20,000 tris | 0.4 µs | 1,090 µs | 654 µs |

Resident meshes cost the same regardless of size (one uniform snapshot + one command); GPU time is separate and was
only measured on a software rasteriser, so it says nothing about real GPUs.

## Dear ImGui
`engine/ui/rlImGui.{hpp,cpp}` is a Dear ImGui 1.92 backend on the renderer with the entry points the game uses from rlImGui
(`rlImGuiSetup/Begin/End/Shutdown`, `rlImGuiSetLoadFontsCallback`, `rlImGuiImageSize`, `rlImGuiImageButtonSize`). It feeds ImGui
mouse/keyboard/text from the compat layer (press and release inside one frame are both delivered), implements ImGui's
texture requests (create / partial update / destroy of the font atlas, so fonts can grow at runtime), and turns draw lists into
scissored, textured draws through raylib's default shader. Dear ImGui itself is vendored in `third_party/imgui` (MIT).
Demo: `examples/imgui_demo.cpp` (`--demo` opens ImGui's full demo window).
Not yet: mouse-cursor shape changes, clipboard, gamepad, multi-viewport.

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
