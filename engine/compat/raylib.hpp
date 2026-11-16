#pragma once
// raylib-compatible API on the Foundry renderer. Port a raylib program with
//     #include "compat/raylib.hpp"
//     using namespace foundry::compat;
// Implemented: window/input/timing, BeginMode3D/EndMode3D, 3D + 2D primitives,
// Mesh/Material/DrawMesh and the GenMesh* helpers. Not yet: text, textures,
// shaders (see docs/ARCHITECTURE.md porting checklist).
#include "compat/raymath.hpp"
#include "platform/Window.hpp"

namespace foundry::compat {

constexpr float PI = 3.14159265358979323846f;
constexpr float DEG2RAD = PI / 180.0f;
constexpr float RAD2DEG = 180.0f / PI;

struct Rectangle { float x, y, width, height; };
struct Camera3D { Vector3 position, target, up; float fovy; int projection; };
using Camera = Camera3D;
enum { CAMERA_PERSPECTIVE = 0, CAMERA_ORTHOGRAPHIC = 1 };

struct Texture { unsigned id; int width, height, mipmaps, format; };
using Texture2D = Texture;
struct Shader { unsigned id; int* locs; };
struct MaterialMap { Texture2D texture; Color color; float value; };
struct Material { Shader shader; MaterialMap* maps; float params[4]; };
enum { MATERIAL_MAP_ALBEDO = 0, MATERIAL_MAP_DIFFUSE = 0, MATERIAL_MAP_METALNESS = 1, MATERIAL_MAP_NORMAL = 2 };

struct Mesh {
    int vertexCount, triangleCount;
    float* vertices; float* texcoords; float* texcoords2; float* normals; float* tangents;
    unsigned char* colors; unsigned short* indices;
    float* animVertices; float* animNormals; unsigned char* boneIds; float* boneWeights;
    Matrix* boneMatrices; int boneCount;
    unsigned vaoId; unsigned* vboId;
};

#define FOUNDRY_COLOR(name, R, G, B, A) inline constexpr Color name{R, G, B, A};
FOUNDRY_COLOR(LIGHTGRAY, 200, 200, 200, 255) FOUNDRY_COLOR(GRAY, 130, 130, 130, 255)
FOUNDRY_COLOR(DARKGRAY, 80, 80, 80, 255)     FOUNDRY_COLOR(YELLOW, 253, 249, 0, 255)
FOUNDRY_COLOR(GOLD, 255, 203, 0, 255)        FOUNDRY_COLOR(ORANGE, 255, 161, 0, 255)
FOUNDRY_COLOR(PINK, 255, 109, 194, 255)      FOUNDRY_COLOR(RED, 230, 41, 55, 255)
FOUNDRY_COLOR(MAROON, 190, 33, 55, 255)      FOUNDRY_COLOR(GREEN, 0, 228, 48, 255)
FOUNDRY_COLOR(LIME, 0, 158, 47, 255)         FOUNDRY_COLOR(DARKGREEN, 0, 117, 44, 255)
FOUNDRY_COLOR(SKYBLUE, 102, 191, 255, 255)   FOUNDRY_COLOR(BLUE, 0, 121, 241, 255)
FOUNDRY_COLOR(DARKBLUE, 0, 82, 172, 255)     FOUNDRY_COLOR(PURPLE, 200, 122, 255, 255)
FOUNDRY_COLOR(VIOLET, 135, 60, 190, 255)     FOUNDRY_COLOR(BROWN, 127, 106, 79, 255)
FOUNDRY_COLOR(WHITE, 255, 255, 255, 255)    FOUNDRY_COLOR(BLACK, 0, 0, 0, 255)
FOUNDRY_COLOR(BLANK, 0, 0, 0, 0)            FOUNDRY_COLOR(RAYWHITE, 245, 245, 245, 255)
#undef FOUNDRY_COLOR

// Key / mouse constants are the same numbers as raylib's.
using namespace foundry::platform;   // KEY_* enums
enum { MOUSE_BUTTON_LEFT = 0, MOUSE_BUTTON_RIGHT = 1, MOUSE_BUTTON_MIDDLE = 2 };

// ---- window / timing ----
void InitWindow(int width, int height, const char* title);
void CloseWindow();
bool WindowShouldClose();
bool IsWindowReady();
bool IsWindowResized();
int GetScreenWidth();
int GetScreenHeight();
int GetRenderWidth();
int GetRenderHeight();
void SetTargetFPS(int fps);          // recorded only; presentation is vsync (FIFO)
int GetFPS();
float GetFrameTime();
double GetTime();
void SetExitKey(int key);
void BeginDrawing();
void EndDrawing();
void ClearBackground(Color c);
void TakeScreenshot(const char* path);   // writes PPM

// ---- input ----
bool IsKeyDown(int key);
bool IsKeyPressed(int key);
bool IsKeyReleased(int key);
bool IsMouseButtonDown(int b);
bool IsMouseButtonPressed(int b);
bool IsMouseButtonReleased(int b);
Vector2 GetMousePosition();
Vector2 GetMouseDelta();
float GetMouseWheelMove();
int GetMouseX();
int GetMouseY();
void EnableCursor();
void DisableCursor();
int GetRandomValue(int min, int max);

// ---- 3D ----
void BeginMode3D(Camera3D camera);
void EndMode3D();
void DrawLine3D(Vector3 a, Vector3 b, Color c);
void DrawTriangle3D(Vector3 a, Vector3 b, Vector3 c, Color col);
void DrawCube(Vector3 pos, float w, float h, float l, Color c);
void DrawCubeV(Vector3 pos, Vector3 size, Color c);
void DrawCubeWires(Vector3 pos, float w, float h, float l, Color c);
void DrawSphere(Vector3 center, float radius, Color c);
void DrawSphereWires(Vector3 center, float radius, int rings, int slices, Color c);
void DrawPlane(Vector3 center, Vector2 size, Color c);
void DrawGrid(int slices, float spacing);
void DrawMesh(Mesh mesh, Material material, Matrix transform);

Mesh GenMeshPlane(float width, float length, int resX, int resZ);
Mesh GenMeshCube(float width, float height, float length);
Mesh GenMeshSphere(float radius, int rings, int slices);
Mesh GenMeshCylinder(float radius, float height, int slices);
Mesh GenMeshCone(float radius, float height, int slices);
void UploadMesh(Mesh* mesh, bool dynamic);
void UnloadMesh(Mesh mesh);
Material LoadMaterialDefault();
void UnloadMaterial(Material m);
void* MemAlloc(unsigned size);
void MemFree(void* p);

// ---- 2D ----
void DrawRectangle(int x, int y, int w, int h, Color c);
void DrawRectangleV(Vector2 pos, Vector2 size, Color c);
void DrawRectangleRec(Rectangle r, Color c);
void DrawRectangleRounded(Rectangle r, float roundness, int segments, Color c);
void DrawLine(int x1, int y1, int x2, int y2, Color c);
void DrawLineV(Vector2 a, Vector2 b, Color c);
void DrawLineEx(Vector2 a, Vector2 b, float thick, Color c);
void DrawCircle(int cx, int cy, float radius, Color c);
void DrawCircleV(Vector2 center, float radius, Color c);
void DrawCircleLines(int cx, int cy, float radius, Color c);
void DrawCircleLinesV(Vector2 center, float radius, Color c);
void DrawTriangle(Vector2 a, Vector2 b, Vector2 c, Color col);

// ---- Foundry lighting extensions (no raylib equivalent) ----
void SetSunLight(Vector3 direction, Color color, float intensity);
void SetAmbientLight(Color sky, Color ground);
void SetShadowBounds(Vector3 center, float radius);   // sun shadow map covers this sphere
void EnableShadows(bool enabled);
bool AddPointLight(Vector3 position, float range, Color color, float intensity);   // per frame

} // namespace foundry::compat
