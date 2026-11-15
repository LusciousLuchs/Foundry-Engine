# Architecture

## Goals
Lightweight, fast, good lighting, reasonable physics, Vulkan primary, OpenGL supported.

## Layers
    core/      math, memory arenas, job system, logging
    platform/  window, input (SDL3)
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
