# Porting a raylib program

1. Replace `#include "raylib.h"` / `"raymath.h"` with `#include "compat/raylib.hpp"` and add `using namespace foundry::compat;`.
2. Build with the Windows toolchain (see ARCHITECTURE.md). Shaders the program loads must be ported once (see "Shaders").
3. Measure what is still missing:

```python
import re, glob, collections
G = "<game repo>/"
hdr = open(G + "third_party/raylib/src/raylib.h").read()
api = set(re.findall(r'RLAPI\s+[\w\s\*]+?\b(\w+)\(', hdr))
use = collections.Counter()
for f in glob.glob(G + "src/**/*.[ch]pp", recursive=True):
    for n in re.findall(r'\b(\w+)\s*\(', open(f, errors="ignore").read()):
        if n in api: use[n] += 1
mine = set(re.findall(r'\b(\w+)\s*\(', open("engine/compat/raylib.hpp").read() + open("engine/compat/raymath.hpp").read()))
print({n: c for n, c in use.items() if n not in mine})
```

Demos that exercise the layer: `examples/raylib_demo.cpp` (built-in lighting), `examples/gunsmith_shader_demo.cpp`
(the game's own four shaders driven exactly as the game does).

## Building Gunsmith3D on Foundry (no game source changes)
```
cmake -S port/gunsmith3d -B build-gunsmith -DGAME_DIR=<Gunsmith3D repo> \
      -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-gcc-toolchain.cmake \
      -DFOUNDRY_RAYLIB_DIR=<Gunsmith3D repo>/third_party/raylib/src
cmake --build build-gunsmith -j
```
`port/gunsmith3d/CMakeLists.txt` mirrors the game's own build (same generated `version.hpp` and `changelog.inc`), links
`foundry_imgui foundry_compat` instead of raylib, and puts `port/gunsmith3d/shim/{raylib,raymath,rlgl,rlImGui}.h` on the
include path. Shaders: run `tools/shaders.py extract <game>/src` then `build` whenever the game's GLSL changes.
Run from a folder containing the game's `assets/` (copy it), e.g. `gunsmith3d.exe --preset blowback --frames 60 --shot out.png`.
