// Render textures, viewport + projection override, and the model transform stack, with known geometry:
// a red sphere up-right and a blue cube down-left, so orientation errors are obvious.
//   rt_demo.exe [--frames N] [--screenshot out.ppm]
#include "compat/raylib.hpp"
#include "gunsmith_shader_strings.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace foundry::compat;

static void scene() {
    DrawSphere({1.0f, 1.0f, 0}, 0.8f, Color{230, 30, 30, 255});
    DrawCube({-1.0f, -1.0f, 0}, 1.4f, 1.4f, 1.4f, Color{30, 60, 230, 255});
}

int main(int argc, char** argv) {
    int maxFrames = -1; const char* shot = nullptr;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--frames") && i + 1 < argc) maxFrames = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--screenshot") && i + 1 < argc) shot = argv[++i];
    }
    InitWindow(1280, 720, "rt demo");
    EnableShadows(false);
    RenderTexture2D rt = LoadRenderTexture(300, 300);
    Camera3D cam{{0, 0, 7}, {0, 0, 0}, {0, 1, 0}, 40, CAMERA_PERSPECTIVE};

    // the game's lit shader (custom shader + back-face culling) drawing into a render target
    Shader lit = LoadShaderFromMemory(GS_LIT_VS, GS_LIT_FS);
    lit.locs[SHADER_LOC_MATRIX_MODEL] = GetShaderLocation(lit, "matModel");
    lit.locs[SHADER_LOC_MATRIX_NORMAL] = GetShaderLocation(lit, "matNormal");
    {
        float dir[3] = {-0.42f, 0.85f, 0.55f}, col[3] = {1.0f, 0.96f, 0.90f}, fill[3] = {0.6f, 0.15f, 0.75f}, fillCol[3] = {0.78f, 0.86f, 1.0f};
        float sky[3] = {0.70f, 0.75f, 0.84f}, gnd[3] = {0.42f, 0.41f, 0.40f}, fog[3] = {30.f, 120.f, 0.0f}, fogC[3] = {0.8f, 0.85f, 0.9f}, view[3] = {0, 0, 7}, spec = 0.5f;
        int n = 0;
        SetShaderValue(lit, GetShaderLocation(lit, "lightDir"), dir, SHADER_UNIFORM_VEC3); SetShaderValue(lit, GetShaderLocation(lit, "lightCol"), col, SHADER_UNIFORM_VEC3);
        SetShaderValue(lit, GetShaderLocation(lit, "fillDir"), fill, SHADER_UNIFORM_VEC3); SetShaderValue(lit, GetShaderLocation(lit, "fillCol"), fillCol, SHADER_UNIFORM_VEC3);
        SetShaderValue(lit, GetShaderLocation(lit, "ambSky"), sky, SHADER_UNIFORM_VEC3); SetShaderValue(lit, GetShaderLocation(lit, "ambGround"), gnd, SHADER_UNIFORM_VEC3);
        SetShaderValue(lit, GetShaderLocation(lit, "fogParam"), fog, SHADER_UNIFORM_VEC3); SetShaderValue(lit, GetShaderLocation(lit, "fogColor"), fogC, SHADER_UNIFORM_VEC3);
        SetShaderValue(lit, GetShaderLocation(lit, "viewPos"), view, SHADER_UNIFORM_VEC3); SetShaderValue(lit, GetShaderLocation(lit, "spec"), &spec, SHADER_UNIFORM_FLOAT);
        SetShaderValue(lit, GetShaderLocation(lit, "nLamps"), &n, SHADER_UNIFORM_INT);
    }
    Material litMat = LoadMaterialDefault(); litMat.shader = lit;
    Mesh cubeMesh = GenMeshCube(1, 1, 1), sphMesh = GenMeshSphere(0.5f, 20, 28);
    RenderTexture2D rt2 = LoadRenderTexture(300, 300);

    int frame = 0;
    while (!WindowShouldClose() && (maxFrames < 0 || frame < maxFrames)) {
        if (shot && frame == (maxFrames > 0 ? maxFrames - 1 : 5)) TakeScreenshot(shot);

        BeginDrawing();
        ClearBackground(Color{20, 22, 28, 255});

        BeginTextureMode(rt2);   // custom shader + culling inside a render target
        ClearBackground(Color{40, 44, 52, 255});
        BeginMode3D(cam);
        litMat.maps[MATERIAL_MAP_DIFFUSE].color = Color{230, 30, 30, 255};
        DrawMesh(sphMesh, litMat, MatrixMultiply(MatrixScale(1.6f, 1.6f, 1.6f), MatrixTranslate(1, 1, 0)));
        litMat.maps[MATERIAL_MAP_DIFFUSE].color = Color{30, 60, 230, 255};
        DrawMesh(cubeMesh, litMat, MatrixMultiply(MatrixRotateY(0.6f), MatrixMultiply(MatrixScale(1.4f, 1.4f, 1.4f), MatrixTranslate(-1, -1, 0))));
        EndMode3D();
        EndTextureMode();

        // 1) offscreen: the scene into a 300x300 texture (inside the frame, like the game)
        BeginTextureMode(rt);
        ClearBackground(Color{40, 44, 52, 255});
        BeginMode3D(cam);
        scene();
        EndMode3D();
        DrawRectangle(0, 0, 60, 20, Color{255, 255, 0, 255});   // 2D inside the target: a yellow tab at ITS top-left
        EndTextureMode();


        // 2) a sub-rectangle of the window (x=40, y=60, 400x300) with its own frustum
        BeginMode3D(cam);
        rlViewport(40, 720 - (60 + 300), 400, 300);       // GL convention: origin bottom-left
        rlMatrixMode(RL_PROJECTION);
        rlLoadIdentity();
        double aspect = 400.0 / 300.0, top = RL_CULL_DISTANCE_NEAR * std::tan(cam.fovy * 0.5 * DEG2RAD), right = top * aspect;
        rlFrustum(-right, right, -top, top, RL_CULL_DISTANCE_NEAR, RL_CULL_DISTANCE_FAR);
        rlMatrixMode(RL_MODELVIEW);
        scene();
        EndMode3D();
        rlViewport(0, 0, 1280, 720);
        DrawRectangleLinesEx({40, 60, 400, 300}, 2, Color{255, 255, 255, 255});

        // 3) the render texture, drawn with the game's flip (negative source height)
        DrawTexturePro(rt.texture, {0, 0, 300, -300}, {500, 60, 300, 300}, {0, 0}, 0, WHITE);
        DrawRectangleLinesEx({500, 60, 300, 300}, 2, Color{255, 255, 255, 255});

        DrawTexturePro(rt2.texture, {0, 0, 300, -300}, {850, 60, 300, 300}, {0, 0}, 0, WHITE);
        DrawRectangleLinesEx({850, 60, 300, 300}, 2, Color{255, 255, 255, 255});

        // 4) model transform: only the first cube is moved
        BeginMode3D(Camera3D{{0, 3, 9}, {0, 0, 0}, {0, 1, 0}, 45, CAMERA_PERSPECTIVE});
        rlPushMatrix();
        rlMultMatrixf(MatrixToFloat(MatrixTranslate(3, 0, 0)));
        DrawCube({0, -4.5f, 0}, 1.2f, 1.2f, 1.2f, Color{30, 220, 60, 255});    // green: shifted to x = +3
        rlPopMatrix();
        DrawCube({0, -4.5f, 0}, 1.2f, 1.2f, 1.2f, Color{240, 220, 20, 255});   // yellow: at x = 0
        EndMode3D();
        EndDrawing();
        ++frame;
    }
    UnloadRenderTexture(rt); UnloadRenderTexture(rt2);
    CloseWindow();
    std::printf("ok: %d frames\n", frame);
    return 0;
}
