# Foundry Engine

A lightweight C++20 3D game engine: Vulkan first, OpenGL-ready, with good
lighting and solid-enough physics. Built so raylib games can be ported over
incrementally.

## Build
    cmake -B build && cmake --build build && ctest --test-dir build

See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) for the design and roadmap.

See [docs/EDITOR.md](docs/EDITOR.md) for the scene editor (GUI + command-line, both edit the same text scene files).

## Status
Gunsmith3D (a ~37,000-line raylib + Dear ImGui program) builds **without source changes** against Foundry (`port/gunsmith3d/`, see
[docs/PORTING.md](docs/PORTING.md)) and runs on Vulkan 1.3 under Wine: designer, test bench, first-person range, map editor, pose
editor, career mode, settings, tutorials. Verified on a software Vulkan driver only; real-GPU testing is the next step.
