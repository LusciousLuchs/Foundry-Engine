# Foundry Engine

A lightweight C++20 3D game engine: Vulkan first, OpenGL-ready, with good
lighting and solid-enough physics. Built so raylib games can be ported over
incrementally.

## Build
    cmake -B build && cmake --build build && ctest --test-dir build

See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) for the design and roadmap.

See [docs/EDITOR.md](docs/EDITOR.md) for the scene editor (GUI + command-line, both edit the same text scene files).
