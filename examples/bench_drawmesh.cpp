// CPU cost of DrawMesh submission (no GPU time): how much does re-sending mesh data every call cost?
#include "compat/raylib.hpp"
#include "gunsmith_shader_strings.hpp"
#include <chrono>
#include <cstdio>
using namespace foundry::compat;
int main() {
    InitWindow(640, 360, "bench");
    Shader lit = LoadShaderFromMemory(GS_LIT_VS, GS_LIT_FS);
    Material custom = LoadMaterialDefault(); custom.shader = lit;
    Material builtin = LoadMaterialDefault();
    Camera3D cam{{6, 4, 6}, {0, 0, 0}, {0, 1, 0}, 45, CAMERA_PERSPECTIVE};
    struct Case { const char* name; int rings, slices; } cases[] = {{"~250 tris", 8, 16}, {"~2k tris", 32, 32}, {"~20k tris", 100, 100}};
    for (auto& c : cases) {
        Mesh m = GenMeshSphere(1, c.rings, c.slices);
        Mesh cpuCopy = m; cpuCopy.vaoId = 0;   // same data, not resident: re-sent on every draw
        for (int pass = 0; pass < 3; ++pass) {
            const Material& mat = pass == 2 ? builtin : custom;
            const Mesh& mesh = pass == 0 ? m : cpuCopy;
            const int calls = std::max(20, 200000 / std::max(1, m.triangleCount));
            double best = 1e9;
            for (int rep = 0; rep < 4; ++rep) {
                BeginDrawing();
                BeginMode3D(cam);
                auto t0 = std::chrono::steady_clock::now();
                for (int i = 0; i < calls; ++i) DrawMesh(mesh, mat, MatrixTranslate(0, 0, 0));
                auto t1 = std::chrono::steady_clock::now();
                EndMode3D();
                EndDrawing();
                best = std::min(best, std::chrono::duration<double, std::micro>(t1 - t0).count() / calls);
            }
            std::printf("%-10s %-26s %8.1f us/call  (%d tris -> %.0f ns/tri)\n", c.name, pass == 0 ? "custom, GPU-resident" : pass == 1 ? "custom, re-sent (old)" : "built-in lit (CPU xform)", best, m.triangleCount, best * 1000.0 / m.triangleCount);
        }
        UnloadMesh(m);
    }
    CloseWindow();
    return 0;
}
