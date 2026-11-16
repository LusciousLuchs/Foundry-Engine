#pragma once
// raylib-compatible API on the Foundry renderer. Port a raylib program with
//     #include "compat/raylib.hpp"
//     using namespace foundry::compat;
// Implemented: window/input/timing, BeginMode3D/EndMode3D, 3D + 2D primitives,
// Mesh/Material/DrawMesh and the GenMesh* helpers. Not yet: text, textures,
// shaders (see docs/ARCHITECTURE.md porting checklist).
#include "compat/raymath.hpp"
#include "platform/Window.hpp"
#include <cstdarg>
#include <cstdlib>

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
struct Image { void* data; int width, height, mipmaps, format; };   // RGBA8 only (PIXELFORMAT_UNCOMPRESSED_R8G8B8A8)
struct Shader { unsigned id; int* locs; };
struct RenderTexture { unsigned id; Texture texture; Texture depth; };
using RenderTexture2D = RenderTexture;
enum { PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 = 7 };
enum { SHADER_UNIFORM_FLOAT = 0, SHADER_UNIFORM_VEC2, SHADER_UNIFORM_VEC3, SHADER_UNIFORM_VEC4, SHADER_UNIFORM_INT,
       SHADER_UNIFORM_IVEC2, SHADER_UNIFORM_IVEC3, SHADER_UNIFORM_IVEC4, SHADER_UNIFORM_SAMPLER2D };
enum { SHADER_LOC_VERTEX_POSITION = 0, SHADER_LOC_VERTEX_TEXCOORD01, SHADER_LOC_VERTEX_TEXCOORD02, SHADER_LOC_VERTEX_NORMAL,
       SHADER_LOC_VERTEX_TANGENT, SHADER_LOC_VERTEX_COLOR, SHADER_LOC_MATRIX_MVP, SHADER_LOC_MATRIX_VIEW,
       SHADER_LOC_MATRIX_PROJECTION, SHADER_LOC_MATRIX_MODEL, SHADER_LOC_MATRIX_NORMAL, SHADER_LOC_VECTOR_VIEW,
       SHADER_LOC_COLOR_DIFFUSE, SHADER_LOC_COLOR_SPECULAR, SHADER_LOC_COLOR_AMBIENT, SHADER_LOC_MAP_ALBEDO };
enum { TEXTURE_FILTER_POINT = 0, TEXTURE_FILTER_BILINEAR, TEXTURE_FILTER_TRILINEAR, TEXTURE_FILTER_ANISOTROPIC_4X,
       TEXTURE_FILTER_ANISOTROPIC_8X, TEXTURE_FILTER_ANISOTROPIC_16X };
enum { TEXTURE_WRAP_REPEAT = 0, TEXTURE_WRAP_CLAMP, TEXTURE_WRAP_MIRROR_REPEAT, TEXTURE_WRAP_MIRROR_CLAMP };
enum { BLEND_ALPHA = 0, BLEND_ADDITIVE, BLEND_MULTIPLIED, BLEND_ADD_COLORS, BLEND_SUBTRACT_COLORS, BLEND_ALPHA_PREMULTIPLY, BLEND_CUSTOM };
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

#define RAYLIB_VERSION_MAJOR 5
#define RAYLIB_VERSION_MINOR 5
#define RAYLIB_VERSION_PATCH 0
#define RAYLIB_VERSION "5.5"
enum { LOG_ALL = 0, LOG_TRACE, LOG_DEBUG, LOG_INFO, LOG_WARNING, LOG_ERROR, LOG_FATAL, LOG_NONE };
enum { RL_OPENGL_11 = 1, RL_OPENGL_21, RL_OPENGL_33, RL_OPENGL_43, RL_OPENGL_ES_20, RL_OPENGL_ES_30 };

// ---- window state ----
enum { FLAG_FULLSCREEN_MODE = 0x2, FLAG_WINDOW_RESIZABLE = 0x4, FLAG_WINDOW_UNDECORATED = 0x8, FLAG_WINDOW_TRANSPARENT = 0x10,
       FLAG_MSAA_4X_HINT = 0x20, FLAG_VSYNC_HINT = 0x40, FLAG_WINDOW_HIDDEN = 0x80, FLAG_WINDOW_ALWAYS_RUN = 0x100,
       FLAG_WINDOW_MINIMIZED = 0x200, FLAG_WINDOW_MAXIMIZED = 0x400, FLAG_WINDOW_UNFOCUSED = 0x800, FLAG_WINDOW_TOPMOST = 0x1000,
       FLAG_WINDOW_HIGHDPI = 0x2000, FLAG_WINDOW_MOUSE_PASSTHROUGH = 0x4000, FLAG_BORDERLESS_WINDOWED_MODE = 0x8000,
       FLAG_INTERLACED_HINT = 0x10000 };
struct FilePathList { unsigned capacity, count; char** paths; };
using TraceLogCallback = void (*)(int logLevel, const char* text, va_list args);
void SetConfigFlags(unsigned flags);                 // before InitWindow
void SetWindowState(unsigned flags);
void ClearWindowState(unsigned flags);
bool IsWindowState(unsigned flag);
bool IsWindowFullscreen();
bool IsWindowFocused();
void ToggleFullscreen();                             // borderless window over the monitor (no mode switch)
void ToggleBorderlessWindowed();
void SetWindowSize(int width, int height);
void SetWindowPosition(int x, int y);
void SetWindowMinSize(int width, int height);
void SetMousePosition(int x, int y);
bool IsCursorHidden();
void ShowCursor();
void HideCursor();
int GetMonitorCount();
int GetCurrentMonitor();
int GetMonitorWidth(int monitor);
int GetMonitorHeight(int monitor);
int GetMonitorRefreshRate(int monitor);
Vector2 GetMonitorPosition(int monitor);
const char* GetMonitorName(int monitor);
void SetClipboardText(const char* text);
const char* GetClipboardText();
void OpenURL(const char* url);
bool IsFileDropped();
FilePathList LoadDroppedFiles();
void UnloadDroppedFiles(FilePathList files);
int GetKeyPressed();                                 // next pressed key code (0 when none)
void SetTraceLogCallback(TraceLogCallback callback);

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
bool IsKeyPressedRepeat(int key);   // OS auto-repeat only (raylib semantics)
int GetCharPressed();               // next typed character, 0 when the queue is empty
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

// ---- text formatting / colors / collision helpers ----
const char* TextFormat(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
inline Color GetColor(unsigned hex) {
    return {(unsigned char)(hex >> 24), (unsigned char)(hex >> 16), (unsigned char)(hex >> 8), (unsigned char)hex};
}
inline bool CheckCollisionPointRec(Vector2 p, Rectangle r) {
    return p.x >= r.x && p.x < r.x + r.width && p.y >= r.y && p.y < r.y + r.height;
}

// ---- picking (pure math; same results as raylib) ----
struct Ray { Vector3 position, direction; };
struct RayCollision { bool hit; float distance; Vector3 point, normal; };

inline Matrix GetCameraProjection(Camera3D cam, float aspect) {
    if (cam.projection == CAMERA_PERSPECTIVE) return MatrixPerspective(cam.fovy * DEG2RAD, aspect, 0.01, 1000.0);
    double top = cam.fovy / 2.0, right = top * aspect;
    return MatrixOrtho(-right, right, -top, top, 0.01, 1000.0);
}
inline Ray GetScreenToWorldRayEx(Vector2 pos, Camera3D cam, int width, int height) {
    float x = 2.0f * pos.x / float(width) - 1.0f, y = 1.0f - 2.0f * pos.y / float(height);
    Matrix proj = GetCameraProjection(cam, float(width) / float(height));
    Matrix view = MatrixLookAt(cam.position, cam.target, cam.up);
    Vector3 nearP = Vector3Unproject({x, y, 0.0f}, proj, view), farP = Vector3Unproject({x, y, 1.0f}, proj, view);
    Ray r{cam.position, Vector3Normalize(Vector3Subtract(farP, nearP))};
    if (cam.projection == CAMERA_ORTHOGRAPHIC) r.position = Vector3Unproject({x, y, -1.0f}, proj, view);
    return r;
}
inline Vector2 GetWorldToScreenEx(Vector3 p, Camera3D cam, int width, int height) {
    Matrix proj = GetCameraProjection(cam, float(width) / float(height));
    Matrix view = MatrixLookAt(cam.position, cam.target, cam.up);
    Vector4 w = QuaternionTransform(QuaternionTransform({p.x, p.y, p.z, 1.0f}, view), proj);
    return {(w.x / w.w + 1.0f) / 2.0f * float(width), (-w.y / w.w + 1.0f) / 2.0f * float(height)};
}
inline RayCollision GetRayCollisionTriangle(Ray ray, Vector3 p1, Vector3 p2, Vector3 p3) {
    constexpr float EPS = 0.000001f;
    RayCollision c{};
    Vector3 e1 = Vector3Subtract(p2, p1), e2 = Vector3Subtract(p3, p1);
    Vector3 p = Vector3CrossProduct(ray.direction, e2);
    float a = Vector3DotProduct(e1, p);
    if (a > -EPS && a < EPS) return c;
    float inv = 1.0f / a;
    Vector3 tv = Vector3Subtract(ray.position, p1);
    float u = Vector3DotProduct(tv, p) * inv;
    if (u < 0.0f || u > 1.0f) return c;
    Vector3 q = Vector3CrossProduct(tv, e1);
    float v = Vector3DotProduct(ray.direction, q) * inv;
    if (v < 0.0f || u + v > 1.0f) return c;
    float t = Vector3DotProduct(e2, q) * inv;
    if (t > EPS) {
        c.hit = true; c.distance = t;
        c.normal = Vector3Normalize(Vector3CrossProduct(e1, e2));
        c.point = Vector3Add(ray.position, Vector3Scale(ray.direction, t));
    }
    return c;
}
RayCollision GetRayCollisionMesh(Ray ray, Mesh mesh, Matrix transform);   // nearest triangle of a CPU-side mesh
Ray GetScreenToWorldRay(Vector2 pos, Camera3D cam);        // uses the window size
Vector2 GetWorldToScreen(Vector3 p, Camera3D cam);

// ---- 3D ----
void BeginMode3D(Camera3D camera);
void BeginMode3DOverlay(Camera3D camera);   // like BeginMode3D, but no depth test (gizmos); close with EndMode3D
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
void UploadMesh(Mesh* mesh, bool dynamic);   // static: device-local GPU copy; dynamic: refresh with UpdateMeshBuffer
void UpdateMeshBuffer(Mesh mesh, int index, const void* data, int dataSize, int offset);   // index: 0 pos, 1 uv, 2 normal, 3 colour (non-indexed meshes)
void UnloadMesh(Mesh mesh);
Material LoadMaterialDefault();
void UnloadMaterial(Material m);
void* MemAlloc(unsigned size);
void MemFree(void* p);

// ---- shaders (precompiled by tools/shaders.py; the game's own GLSL source strings are matched by hash) ----
Shader LoadShaderFromMemory(const char* vsCode, const char* fsCode);   // returns id 0 if the pair was not ported
bool IsShaderReady(Shader shader);
int GetShaderLocation(Shader shader, const char* uniformName);          // -1 when absent
void SetShaderValue(Shader shader, int locIndex, const void* value, int uniformType);
void SetShaderValueV(Shader shader, int locIndex, const void* value, int uniformType, int count);
void SetShaderValueMatrix(Shader shader, int locIndex, Matrix mat);
void UnloadShader(Shader shader);
void BeginShaderMode(Shader shader);   // 2D draws (rectangles, textures) and DrawMesh use it until EndShaderMode
void EndShaderMode();

// ---- fonts (stb_truetype; atlas is built at load time) ----
struct GlyphInfo { int value, offsetX, offsetY, advanceX; Image image; };
struct Font { int baseSize, glyphCount, glyphPadding; Texture2D texture; Rectangle* recs; GlyphInfo* glyphs; };
Font LoadFontFromMemory(const char* fileType, const unsigned char* data, int dataSize, int fontSize, int* codepoints, int codepointCount);
Font LoadFontEx(const char* fileName, int fontSize, int* codepoints, int codepointCount);
bool IsFontReady(Font font);
void UnloadFont(Font font);
void DrawTextEx(Font font, const char* text, Vector2 position, float fontSize, float spacing, Color tint);
Vector2 MeasureTextEx(Font font, const char* text, float fontSize, float spacing);

// ---- files / compression ----
Image LoadImage(const char* fileName);                                            // png jpg bmp tga (stb_image)
Image LoadImageFromMemory(const char* fileType, const unsigned char* data, int dataSize);
Image GenImageChecked(int width, int height, int checksX, int checksY, Color c1, Color c2);
void ImageResize(Image* image, int newWidth, int newHeight);                      // bilinear
void ImageFormat(Image* image, int newFormat);                                    // only R8G8B8A8 exists: a no-op

// ---- images / textures ----
Image GenImageColor(int width, int height, Color color);
void ImageDrawRectangle(Image* dst, int x, int y, int w, int h, Color color);
void ImageDrawCircle(Image* dst, int cx, int cy, int radius, Color color);
void UnloadImage(Image image);
Texture2D LoadTextureFromImage(Image image);
void GenTextureMipmaps(Texture2D* texture);
void SetTextureFilter(Texture2D texture, int filter);
void SetTextureWrap(Texture2D texture, int wrap);
void UnloadTexture(Texture2D texture);
void DrawTexturePro(Texture2D tex, Rectangle src, Rectangle dst, Vector2 origin, float rotation, Color tint);

// ---- render textures: BeginTextureMode(rt) ... draws ... EndTextureMode(); sample rt.texture (v is flipped, as in GL) ----
RenderTexture2D LoadRenderTexture(int width, int height);
bool IsRenderTextureReady(RenderTexture2D target);
void UnloadRenderTexture(RenderTexture2D target);
void BeginTextureMode(RenderTexture2D target);
void EndTextureMode();

// ---- rlgl matrix stack / viewport (subset used with BeginMode3D) ----
enum { RL_MODELVIEW = 0x1700, RL_PROJECTION = 0x1701, RL_TEXTURE = 0x1702 };
enum { RL_DEFAULT_SHADER_ATTRIB_LOCATION_POSITION = 0, RL_DEFAULT_SHADER_ATTRIB_LOCATION_TEXCOORD = 1,
       RL_DEFAULT_SHADER_ATTRIB_LOCATION_NORMAL = 2, RL_DEFAULT_SHADER_ATTRIB_LOCATION_COLOR = 3 };
inline constexpr double RL_CULL_DISTANCE_NEAR = 0.01, RL_CULL_DISTANCE_FAR = 1000.0;
#define RL_MALLOC(sz) std::malloc(sz)
#define RL_CALLOC(n, sz) std::calloc(n, sz)
#define RL_REALLOC(p, sz) std::realloc(p, sz)
#define RL_FREE(p) std::free(p)
#define MatrixToFloat(mat) (foundry::compat::MatrixToFloatV(mat).v)
void rlViewport(int x, int y, int width, int height);   // GL convention: origin bottom-left of the target
void rlMatrixMode(int mode);
void rlLoadIdentity();
void rlPushMatrix();
void rlPopMatrix();
void rlMultMatrixf(const float* matf);
void rlFrustum(double left, double right, double bottom, double top, double znear, double zfar);
void rlOrtho(double left, double right, double bottom, double top, double znear, double zfar);
int rlGetVersion();
inline void rlUnloadVertexArray(unsigned) {}
inline void rlUnloadVertexBuffer(unsigned) {}

// ---- render state (rlgl) ----
void rlEnableDepthMask();
void rlDisableDepthMask();
void rlEnableDepthTest();
void rlDisableDepthTest();
void rlEnableBackfaceCulling();
void rlDisableBackfaceCulling();
void rlDrawRenderBatchActive();   // no-op: draws are batched and ordered automatically
void BeginBlendMode(int mode);
void EndBlendMode();
void BeginScissorMode(int x, int y, int width, int height);
void EndScissorMode();

// ---- 2D ----
void DrawRectangle(int x, int y, int w, int h, Color c);
void DrawRectangleV(Vector2 pos, Vector2 size, Color c);
void DrawRectangleRec(Rectangle r, Color c);
void DrawRectangleRounded(Rectangle r, float roundness, int segments, Color c);
void DrawRectangleLinesEx(Rectangle r, float thick, Color c);
void DrawRectangleGradientV(int x, int y, int w, int h, Color top, Color bottom);
void DrawRectangleGradientH(int x, int y, int w, int h, Color left, Color right);
// Built-in monospace bitmap font (DejaVu Sans Mono derived). fontSize 12 draws 1:1 (7x15 px cells).
void DrawText(const char* text, int x, int y, int fontSize, Color c);
int MeasureText(const char* text, int fontSize);
void DrawLine(int x1, int y1, int x2, int y2, Color c);
void DrawLineV(Vector2 a, Vector2 b, Color c);
void DrawLineEx(Vector2 a, Vector2 b, float thick, Color c);
void DrawCircle(int cx, int cy, float radius, Color c);
void DrawCircleV(Vector2 center, float radius, Color c);
void DrawRing(Vector2 center, float innerRadius, float outerRadius, float startAngle, float endAngle, int segments, Color color);
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

extern "C" {   // file-name helpers with C linkage (raylib's rcore; also used by raudio.c)
const char* GetFileExtension(const char* fileName);          // includes the dot; nullptr when there is none
bool IsFileExtension(const char* fileName, const char* ext); // case-insensitive; ext may be ".a;.b"
const char* GetFileName(const char* filePath);
const char* GetFileNameWithoutExt(const char* filePath);
unsigned char* DecompressData(const unsigned char* compData, int compDataSize, int* dataSize);   // DEFLATE (raylib: C linkage)
}

#include "compat/audio.hpp"   // raylib's audio API (needs the foundry_audio library: configure with -DFOUNDRY_RAYLIB_DIR=...)
