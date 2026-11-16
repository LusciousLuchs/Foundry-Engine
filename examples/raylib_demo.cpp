// A raylib-style program: only the compat API is used, nothing Vulkan-specific.
//   raylib_demo.exe [--frames N] [--screenshot out.ppm]
#include "compat/raylib.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace foundry::compat;

int main(int argc, char** argv) {
    int maxFrames = -1;
    const char* shot = nullptr;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--frames") && i + 1 < argc) maxFrames = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--screenshot") && i + 1 < argc) shot = argv[++i];
    }

    InitWindow(1280, 720, "Foundry - raylib-style demo");
    SetTargetFPS(60);

    Camera3D camera{{9, 6, 9}, {0, 1, 0}, {0, 1, 0}, 50.0f, CAMERA_PERSPECTIVE};
    Mesh cyl = GenMeshCylinder(0.5f, 2.0f, 24);
    Mesh cone = GenMeshCone(0.7f, 1.6f, 24);
    Material mat = LoadMaterialDefault();

    int frame = 0;
    while (!WindowShouldClose() && (maxFrames < 0 || frame < maxFrames)) {
        float t = (maxFrames > 0) ? float(frame) * 0.05f : float(GetTime());
        float orbit = 0.25f * t;
        camera.position = {std::cos(orbit) * 11.0f, 5.5f, std::sin(orbit) * 11.0f};

        if (IsKeyPressed(KEY_SPACE)) TakeScreenshot("demo.ppm");
        if (shot && frame == (maxFrames > 0 ? maxFrames - 1 : 5)) TakeScreenshot(shot);

        BeginDrawing();
        ClearBackground({22, 26, 36, 255});

        // lights: warm sun (default) + two orbiting coloured point lights
        Vector3 l1{std::cos(t * 1.3f) * 4.0f, 1.6f, std::sin(t * 1.3f) * 4.0f};
        Vector3 l2{std::cos(t * 0.7f + 3.0f) * 3.0f, 2.4f, std::sin(t * 0.7f + 3.0f) * 3.0f};
        AddPointLight(l1, 9.0f, {255, 90, 60, 255}, 14.0f);
        AddPointLight(l2, 9.0f, {80, 140, 255, 255}, 14.0f);

        BeginMode3D(camera);
        DrawPlane({0, 0, 0}, {24, 24}, {150, 150, 155, 255});
        DrawGrid(24, 1.0f);

        DrawCube({-3, 1, 0}, 2, 2, 2, {200, 120, 60, 255});
        DrawCubeWires({-3, 1, 0}, 2.02f, 2.02f, 2.02f, BLACK);
        DrawSphere({0, 1.2f, 0}, 1.2f, {220, 220, 230, 255});
        DrawSphere({3.2f, 0.8f, 1.5f}, 0.8f, {90, 200, 120, 255});
        DrawMesh(cyl, mat, MatrixMultiply(MatrixRotateY(t), MatrixTranslate(3, 0, -3)));
        DrawMesh(cone, mat, MatrixMultiply(MatrixRotateY(-t), MatrixTranslate(-3, 0, 3.5f)));
        DrawTriangle3D({-1, 0.01f, -4}, {1, 0.01f, -4}, {0, 2.2f, -4}, {240, 200, 60, 255});

        // light markers (unlit sphere stand-ins)
        DrawSphereWires(l1, 0.25f, 6, 8, {255, 120, 90, 255});
        DrawSphereWires(l2, 0.25f, 6, 8, {120, 170, 255, 255});
        EndMode3D();

        DrawRectangleRounded({16, 16, 260, 74}, 0.3f, 16, {0, 0, 0, 150});
        DrawCircleV({44, 53}, 14, {255, 90, 60, 255});
        DrawCircleV({92, 53}, 14, {80, 140, 255, 255});
        DrawRectangle(120, 44, int(120 + 10 * std::sin(t * 3)), 18, {90, 200, 120, 255});
        DrawLineEx({16, 710}, {1264, 710}, 2.0f, {255, 255, 255, 120});
        EndDrawing();
        ++frame;
    }
    UnloadMesh(cyl); UnloadMesh(cone); UnloadMaterial(mat);
    CloseWindow();
    std::printf("ok: %d frames\n", frame);
    return 0;
}
