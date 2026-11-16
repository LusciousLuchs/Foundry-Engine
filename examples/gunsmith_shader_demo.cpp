// Drives the game's real shaders through the compat API exactly as Gunsmith3D does:
// LoadShaderFromMemory(<its own GLSL>), GetShaderLocation, SetShaderValue(V), BeginShaderMode,
// DrawMesh with a shader material, rlDisableDepthMask/BackfaceCulling for the grid, a generated texture.
//   gunsmith_shader_demo.exe [--frames N] [--screenshot out.ppm]
#include "compat/raylib.hpp"
#include "gunsmith_shader_strings.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace foundry::compat;

namespace {
struct { Shader lit{}; int locView, locFog, locSpec, locLight, locLightCol, locFill, locFillCol, locAmbSky, locAmbGround, locFogParam, locNLamps, locLampPos, locLampCol, locLampDir; } G;

void initLit() {   // (initGfx in the game)
    G.lit = LoadShaderFromMemory(GS_LIT_VS, GS_LIT_FS);
    G.lit.locs[SHADER_LOC_MATRIX_MODEL] = GetShaderLocation(G.lit, "matModel");
    G.lit.locs[SHADER_LOC_MATRIX_NORMAL] = GetShaderLocation(G.lit, "matNormal");
    G.locView = GetShaderLocation(G.lit, "viewPos");
    G.locFog = GetShaderLocation(G.lit, "fogColor");
    G.locSpec = GetShaderLocation(G.lit, "spec");
    G.locLight = GetShaderLocation(G.lit, "lightDir"); G.locLightCol = GetShaderLocation(G.lit, "lightCol");
    G.locFill = GetShaderLocation(G.lit, "fillDir"); G.locFillCol = GetShaderLocation(G.lit, "fillCol");
    G.locAmbSky = GetShaderLocation(G.lit, "ambSky"); G.locAmbGround = GetShaderLocation(G.lit, "ambGround");
    G.locFogParam = GetShaderLocation(G.lit, "fogParam");
    G.locNLamps = GetShaderLocation(G.lit, "nLamps");
    G.locLampPos = GetShaderLocation(G.lit, "lampPos"); G.locLampCol = GetShaderLocation(G.lit, "lampCol"); G.locLampDir = GetShaderLocation(G.lit, "lampDir");
}
void setLighting() {   // the game's default workshop Lighting
    float dir[3] = {-0.42f, 0.85f, 0.55f}, col[3] = {1.0f, 0.96f, 0.90f}, fill[3] = {0.6f, 0.15f, 0.75f}, fillCol[3] = {0.78f, 0.86f, 1.0f};
    float sky[3] = {0.70f, 0.75f, 0.84f}, gnd[3] = {0.42f, 0.41f, 0.40f}, fog[3] = {30.f, 120.f, 0.65f}, fogC[3] = {199 / 255.f, 219 / 255.f, 239 / 255.f};
    SetShaderValue(G.lit, G.locLight, dir, SHADER_UNIFORM_VEC3);
    SetShaderValue(G.lit, G.locLightCol, col, SHADER_UNIFORM_VEC3);
    SetShaderValue(G.lit, G.locFill, fill, SHADER_UNIFORM_VEC3);
    SetShaderValue(G.lit, G.locFillCol, fillCol, SHADER_UNIFORM_VEC3);
    SetShaderValue(G.lit, G.locAmbSky, sky, SHADER_UNIFORM_VEC3);
    SetShaderValue(G.lit, G.locAmbGround, gnd, SHADER_UNIFORM_VEC3);
    SetShaderValue(G.lit, G.locFogParam, fog, SHADER_UNIFORM_VEC3);
    SetShaderValue(G.lit, G.locFog, fogC, SHADER_UNIFORM_VEC3);
}
void setLamps(float t) {   // two lamps, like a map's lights (arrays of vec4, SetShaderValueV)
    float P[16 * 4] = {0}, C[16 * 4] = {0}, D[16 * 4] = {0};
    float pos[2][3] = {{std::cos(t) * 4.0f, 1.6f, std::sin(t) * 4.0f}, {-3.5f, 2.2f, -1.0f}};
    float col[2][3] = {{1.0f, 0.45f, 0.25f}, {0.3f, 0.55f, 1.0f}};
    for (int i = 0; i < 2; ++i) {
        for (int k = 0; k < 3; ++k) { P[i * 4 + k] = pos[i][k]; C[i * 4 + k] = col[i][k] * 6.0f; D[i * 4 + k] = (k == 1) ? -1.0f : 0.0f; }
        P[i * 4 + 3] = 9.0f; D[i * 4 + 3] = -2.0f;   // range, no cone
    }
    int n = 2;
    SetShaderValue(G.lit, G.locNLamps, &n, SHADER_UNIFORM_INT);
    SetShaderValueV(G.lit, G.locLampPos, P, SHADER_UNIFORM_VEC4, n);
    SetShaderValueV(G.lit, G.locLampCol, C, SHADER_UNIFORM_VEC4, n);
    SetShaderValueV(G.lit, G.locLampDir, D, SHADER_UNIFORM_VEC4, n);
}
} // namespace

int main(int argc, char** argv) {
    int maxFrames = -1; const char* shot = nullptr;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--frames") && i + 1 < argc) maxFrames = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--screenshot") && i + 1 < argc) shot = argv[++i];
    }
    InitWindow(1280, 720, "Gunsmith shaders on Foundry");
    initLit();
    Shader bg = LoadShaderFromMemory(GS_BG_VS, GS_BG_FS);
    int bRect = GetShaderLocation(bg, "rect"), bInner = GetShaderLocation(bg, "cInner"), bOuter = GetShaderLocation(bg, "cOuter"),
        bRadial = GetShaderLocation(bg, "radial"), bGrain = GetShaderLocation(bg, "grain");
    Shader grid = LoadShaderFromMemory(GS_GRID_VS, GS_GRID_FS);
    grid.locs[SHADER_LOC_MATRIX_MODEL] = GetShaderLocation(grid, "matModel");
    int gPlane = GetShaderLocation(grid, "plane"), gView = GetShaderLocation(grid, "viewPos"), gBase = GetShaderLocation(grid, "cBase"),
        gFine = GetShaderLocation(grid, "cFine"), gMinor = GetShaderLocation(grid, "cMinor"), gMajor = GetShaderLocation(grid, "cMajor"),
        gSteps = GetShaderLocation(grid, "steps"), gCentre = GetShaderLocation(grid, "centre"), gRadius = GetShaderLocation(grid, "radius");
    Shader round = LoadShaderFromMemory(nullptr, GS_ROUND_FS);   // default vertex shader, like range.cpp
    if (!IsShaderReady(G.lit) || !IsShaderReady(bg) || !IsShaderReady(grid) || !IsShaderReady(round)) { std::fprintf(stderr, "a game shader failed to load\n"); return 1; }

    // textures: the game's studded baseplate tile
    Image img = GenImageColor(32, 32, Color{124, 132, 140, 255});
    ImageDrawRectangle(&img, 0, 0, 32, 1, Color{104, 111, 119, 255});
    ImageDrawRectangle(&img, 0, 0, 1, 32, Color{104, 111, 119, 255});
    ImageDrawCircle(&img, 16, 16, 9, Color{134, 142, 150, 255});
    ImageDrawCircle(&img, 15, 15, 7, Color{142, 150, 158, 255});
    Texture2D plateTex = LoadTextureFromImage(img);
    UnloadImage(img);
    GenTextureMipmaps(&plateTex);
    SetTextureFilter(plateTex, TEXTURE_FILTER_TRILINEAR);
    SetTextureWrap(plateTex, TEXTURE_WRAP_REPEAT);

    Mesh cube = GenMeshCube(1, 1, 1), cyl = GenMeshCylinder(0.5f, 1, 28), cone = GenMeshCone(0.5f, 1, 28), sphere = GenMeshSphere(0.5f, 20, 28);
    Mesh plate{};   // a big textured quad, uv = position (one tile per unit), like the game's plate
    {
        const float S = 40.f; float V[6][2] = {{-S, -S}, {S, S}, {S, -S}, {-S, -S}, {-S, S}, {S, S}};
        plate.vertexCount = 6; plate.triangleCount = 2;
        plate.vertices = (float*)MemAlloc(18 * sizeof(float)); plate.normals = (float*)MemAlloc(18 * sizeof(float)); plate.texcoords = (float*)MemAlloc(12 * sizeof(float));
        for (int i = 0; i < 6; ++i) {
            plate.vertices[i * 3] = V[i][0]; plate.vertices[i * 3 + 1] = 0; plate.vertices[i * 3 + 2] = V[i][1];
            plate.normals[i * 3] = 0; plate.normals[i * 3 + 1] = 1; plate.normals[i * 3 + 2] = 0;
            plate.texcoords[i * 2] = V[i][0]; plate.texcoords[i * 2 + 1] = V[i][1];
        }
    }
    Material mat = LoadMaterialDefault(); mat.shader = G.lit;
    Material plateMat = LoadMaterialDefault(); plateMat.shader = G.lit; plateMat.maps[MATERIAL_MAP_DIFFUSE].texture = plateTex;
    Material gridMat = LoadMaterialDefault(); gridMat.shader = grid;
    Mesh quad = GenMeshPlane(1, 1, 1, 1);

    Camera3D cam{{9, 5.5f, 9}, {0, 1.2f, 0}, {0, 1, 0}, 45.0f, CAMERA_PERSPECTIVE};
    auto drawMesh = [&](const Mesh& m, Matrix M, Color c, float spec) {
        SetShaderValue(G.lit, G.locSpec, &spec, SHADER_UNIFORM_FLOAT);
        mat.maps[MATERIAL_MAP_DIFFUSE].color = c;
        DrawMesh(m, mat, M);
    };

    int frame = 0;
    while (!WindowShouldClose() && (maxFrames < 0 || frame < maxFrames)) {
        float t = (maxFrames > 0) ? float(frame) * 0.05f : float(GetTime());
        cam.position = {std::cos(0.2f * t) * 11.0f, 5.5f, std::sin(0.2f * t) * 11.0f};
        if (shot && frame == (maxFrames > 0 ? maxFrames - 1 : 5)) TakeScreenshot(shot);

        BeginDrawing();
        ClearBackground(BLACK);

        // the game's backdrop: a full-screen rectangle through the BG shader
        {
            float W = float(GetRenderWidth()), H = float(GetRenderHeight());
            float rect[4] = {0, H - H, W, H}, inner[3] = {0.46f, 0.67f, 0.87f}, outer[3] = {0.84f, 0.90f, 0.95f}, radial = 0.0f, grain = 0.004f;
            SetShaderValue(bg, bRect, rect, SHADER_UNIFORM_VEC4); SetShaderValue(bg, bInner, inner, SHADER_UNIFORM_VEC3);
            SetShaderValue(bg, bOuter, outer, SHADER_UNIFORM_VEC3); SetShaderValue(bg, bRadial, &radial, SHADER_UNIFORM_FLOAT);
            SetShaderValue(bg, bGrain, &grain, SHADER_UNIFORM_FLOAT);
            BeginShaderMode(bg);
            DrawRectangle(0, 0, int(W), int(H), WHITE);
            EndShaderMode();
        }

        BeginMode3D(cam);
        float v[3] = {cam.position.x, cam.position.y, cam.position.z};
        SetShaderValue(G.lit, G.locView, v, SHADER_UNIFORM_VEC3);
        setLighting(); setLamps(t * 1.4f);

        { float matte = 0.0f; SetShaderValue(G.lit, G.locSpec, &matte, SHADER_UNIFORM_FLOAT); DrawMesh(plate, plateMat, MatrixTranslate(0, -0.001f, 0)); }
        drawMesh(cube, MatrixMultiply(MatrixScale(3, 2, 2), MatrixTranslate(-2.5f, 1, 0)), Color{200, 120, 60, 255}, 0.3f);
        drawMesh(sphere, MatrixMultiply(MatrixScale(2.4f, 2.4f, 2.4f), MatrixTranslate(1.0f, 1.2f, 1.5f)), Color{225, 225, 235, 255}, 0.9f);
        drawMesh(cyl, MatrixMultiply(MatrixMultiply(MatrixScale(0.5f, 4, 0.5f), MatrixRotateZ(PI / 2)), MatrixTranslate(2.0f, 0.6f, -2.5f)), Color{95, 100, 110, 255}, 0.8f);
        drawMesh(cone, MatrixMultiply(MatrixRotateY(t), MatrixMultiply(MatrixScale(1.4f, 2.0f, 1.4f), MatrixTranslate(4.2f, 0, -1))), Color{230, 180, 50, 255}, 0.2f);
        drawMesh(sphere, MatrixMultiply(MatrixScale(1.1f, 1.1f, 1.1f), MatrixTranslate(-1.0f, 0.55f, 3.5f)), Color{90, 200, 120, 160}, 0.5f);   // translucent (alpha blend)

        // the game's grid: a quad through the GRID shader, depth mask and culling off
        {
            int plane = 1; float st[3] = {0.1f, 1.0f, 5.0f}, c2[2] = {0, 0}, radius = 14.0f;
            float base[4] = {0, 0, 0, 0}, fine[4] = {0, 0, 0, 0.05f}, minor[4] = {0, 0, 0, 0.14f}, major[4] = {0, 0, 0, 0.32f};
            SetShaderValue(grid, gPlane, &plane, SHADER_UNIFORM_INT); SetShaderValue(grid, gView, v, SHADER_UNIFORM_VEC3);
            SetShaderValue(grid, gBase, base, SHADER_UNIFORM_VEC4); SetShaderValue(grid, gFine, fine, SHADER_UNIFORM_VEC4);
            SetShaderValue(grid, gMinor, minor, SHADER_UNIFORM_VEC4); SetShaderValue(grid, gMajor, major, SHADER_UNIFORM_VEC4);
            SetShaderValue(grid, gSteps, st, SHADER_UNIFORM_VEC3); SetShaderValue(grid, gCentre, c2, SHADER_UNIFORM_VEC2);
            SetShaderValue(grid, gRadius, &radius, SHADER_UNIFORM_FLOAT);
            rlDisableBackfaceCulling(); rlDisableDepthMask();
            DrawMesh(quad, gridMat, MatrixMultiply(MatrixScale(radius * 2.2f, 1, radius * 2.2f), MatrixTranslate(0, 0.01f, 0)));
            rlEnableDepthMask(); rlEnableBackfaceCulling();
        }
        EndMode3D();

        // the scope eyepiece: a texture clipped to a circle by the 'round' shader
        BeginShaderMode(round);
        DrawTexturePro(plateTex, {0, 0, 32, 32}, {1070, 40, 170, 170}, {0, 0}, 0, Color{255, 255, 255, 235});
        EndShaderMode();
        DrawRectangleGradientV(0, 700, 1280, 20, Color{0, 0, 0, 0}, Color{0, 0, 0, 140});
        EndDrawing();
        ++frame;
    }
    UnloadShader(G.lit); UnloadShader(bg); UnloadShader(grid); UnloadShader(round);
    CloseWindow();
    std::printf("ok: %d frames\n", frame);
    return 0;
}
