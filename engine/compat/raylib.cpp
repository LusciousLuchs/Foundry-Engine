// Keep windows.h (pulled in by the Vulkan Win32 header) from declaring
// CloseWindow/DrawText/Rectangle/etc. that collide with the raylib names.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#define NOUSER
#define NOGDI
#define NOMINMAX
#include "compat/raylib.hpp"
#include "render/Renderer.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <utility>
#include <vector>

namespace foundry::compat {
using render::Vertex2D;
using render::Vertex3D;

namespace {
struct State {
    platform::Window win;
    render::Renderer rend;
    bool ready = false, inFrame = false, resized = false;
    int exitKey = KEY_ESCAPE;
    int targetFps = 0;
    std::chrono::steady_clock::time_point start, lastFrame;
    float frameTime = 1.0f / 60.0f;
    float clear[4] = {0.96f, 0.96f, 0.96f, 1.0f};
    std::mt19937 rng{std::random_device{}()};
    std::string shot;
    int fpsFrames = 0; double fpsStamp = 0; int fps = 0;
    int activeShader = 0;                    // BeginShaderMode
    Matrix view = MatrixIdentity(), proj = MatrixIdentity();
    bool in3D = false;
    int defaultShader = 0;                   // raylib's default textured shader, created on demand
};
State& S() { static State s; return s; }

Matrix transposeM(Matrix m) {
    return {m.m0, m.m1, m.m2, m.m3, m.m4, m.m5, m.m6, m.m7, m.m8, m.m9, m.m10, m.m11, m.m12, m.m13, m.m14, m.m15};
}
void toFloats(Matrix m, float out[16]) { float16 f = MatrixToFloatV(m); std::memcpy(out, f.v, 64); }
int defaultShaderId() {
    State& s = S();
    if (!s.defaultShader)
        if (const render::PortedShader* p = render::findPortedShaderByName("builtin_default")) s.defaultShader = s.rend.createShader(*p);
    return s.defaultShader;
}
// A screen-space quad through a custom shader (rectangles, textured quads). Positions are pixels.
void quad2DCustom(int shader, int tex, const Vector2 p[4], const Vector2 uv[4], Color c) {
    State& s = S();
    render::StdUniforms u{};
    Matrix o = MatrixOrtho(0, s.win.width(), s.win.height(), 0, 0.0, 1.0);
    toFloats(o, u.mvp); toFloats(o, u.proj); toFloats(MatrixIdentity(), u.model); toFloats(MatrixIdentity(), u.normal); toFloats(MatrixIdentity(), u.view);
    u.diffuse[0] = u.diffuse[1] = u.diffuse[2] = u.diffuse[3] = 1.0f;
    Vertex3D v[6];
    const int order[6] = {0, 1, 2, 0, 2, 3};   // TL BL BR / TL BR TR
    for (int i = 0; i < 6; ++i) { const int k = order[i]; v[i] = {p[k].x, p[k].y, 0, uv[k].x, uv[k].y, 0, 0, 1, c.r, c.g, c.b, c.a}; }
    s.rend.setCullBack(false);
    s.rend.drawCustom(shader, v, 6, render::Topo::Triangles, u, tex);
    s.rend.setCullBack(true);
}
void rectCustom(float x, float y, float w, float h, Color c) {
    Vector2 p[4] = {{x, y}, {x, y + h}, {x + w, y + h}, {x + w, y}}, uv[4] = {{0, 0}, {0, 1}, {1, 1}, {1, 0}};
    quad2DCustom(S().activeShader, 1, p, uv, c);
}
Vertex3D V3(Vector3 p, Vector3 n, Color c) { return {p.x, p.y, p.z, 0.0f, 0.0f, n.x, n.y, n.z, c.r, c.g, c.b, c.a}; }
Vertex2D V2(float x, float y, Color c) { return {x, y, c.r, c.g, c.b, c.a}; }
Vector3 faceNormal(Vector3 a, Vector3 b, Vector3 c) {
    return Vector3Normalize(Vector3CrossProduct(Vector3Subtract(b, a), Vector3Subtract(c, a)));
}
} // namespace

// ================= window / timing =================
void InitWindow(int w, int h, const char* title) {
    State& s = S();
    if (s.ready) return;
    if (!s.win.create(title, w, h)) { std::fprintf(stderr, "InitWindow: window creation failed\n"); std::exit(1); }
    if (!s.rend.init(s.win)) { std::fprintf(stderr, "InitWindow: Vulkan 1.3 not available\n"); std::exit(1); }
    s.ready = true;
    {   // shader locations are raylib-style small arrays
    }
    s.start = s.lastFrame = std::chrono::steady_clock::now();
    s.win.pollEvents();
}
void CloseWindow() { State& s = S(); s.rend.shutdown(); s.win.destroy(); s.ready = false; }
bool WindowShouldClose() { State& s = S(); return s.win.shouldClose() || (s.exitKey && s.win.isKeyPressed(s.exitKey)); }
bool IsWindowReady() { return S().ready; }
bool IsWindowResized() { return S().resized; }
int GetScreenWidth() { return S().win.width(); }
int GetScreenHeight() { return S().win.height(); }
int GetRenderWidth() { return S().win.width(); }
int GetRenderHeight() { return S().win.height(); }
void SetTargetFPS(int fps) { S().targetFps = fps; }
int GetFPS() { return S().fps; }
float GetFrameTime() { return S().frameTime; }
double GetTime() { return std::chrono::duration<double>(std::chrono::steady_clock::now() - S().start).count(); }
void SetExitKey(int k) { S().exitKey = k; }
void TakeScreenshot(const char* path) { S().shot = path; }

void ClearBackground(Color c) {
    State& s = S();
    s.clear[0] = c.r / 255.0f; s.clear[1] = c.g / 255.0f; s.clear[2] = c.b / 255.0f; s.clear[3] = c.a / 255.0f;
}
void BeginDrawing() {
    State& s = S();
    s.resized = s.win.resizedFlag();   // the renderer consumes it and rebuilds the swapchain
    s.inFrame = s.rend.beginFrame(s.clear[0], s.clear[1], s.clear[2], s.clear[3]);
}
void EndDrawing() {
    State& s = S();
    if (s.inFrame) {
        s.rend.setClear(s.clear[0], s.clear[1], s.clear[2], s.clear[3]);
        if (!s.shot.empty()) { s.rend.requestScreenshot(s.shot); s.shot.clear(); }
        s.rend.endFrame();
        s.inFrame = false;
    }
    s.win.pollEvents();
    auto now = std::chrono::steady_clock::now();
    s.frameTime = std::chrono::duration<float>(now - s.lastFrame).count();
    s.lastFrame = now;
    ++s.fpsFrames;
    double t = GetTime();
    if (t - s.fpsStamp >= 1.0) { s.fps = int(s.fpsFrames / (t - s.fpsStamp)); s.fpsFrames = 0; s.fpsStamp = t; }
}

// ================= input =================
bool IsKeyDown(int k) { return S().win.isKeyDown(k); }
bool IsKeyPressed(int k) { return S().win.isKeyPressed(k); }
bool IsKeyReleased(int k) { return S().win.isKeyReleased(k); }
bool IsKeyPressedRepeat(int k) { return S().win.isKeyRepeated(k); }
int GetCharPressed() { return int(S().win.popChar()); }
bool IsMouseButtonDown(int b) { return S().win.isMouseDown(b); }
bool IsMouseButtonPressed(int b) { return S().win.isMousePressed(b); }
bool IsMouseButtonReleased(int b) { return S().win.isMouseReleased(b); }
Vector2 GetMousePosition() { return {S().win.mouseX(), S().win.mouseY()}; }
Vector2 GetMouseDelta() { return {S().win.mouseDX(), S().win.mouseDY()}; }
float GetMouseWheelMove() { return S().win.mouseWheel(); }
int GetMouseX() { return int(S().win.mouseX()); }
int GetMouseY() { return int(S().win.mouseY()); }
void EnableCursor() { S().win.enableCursor(); }
void DisableCursor() { S().win.disableCursor(); }
int GetRandomValue(int lo, int hi) {
    if (lo > hi) std::swap(lo, hi);
    return std::uniform_int_distribution<int>(lo, hi)(S().rng);
}
void* MemAlloc(unsigned size) { return std::calloc(1, size); }
void MemFree(void* p) { std::free(p); }

const char* TextFormat(const char* fmt, ...) {
    static char bufs[4][1024]; static int idx = 0;
    char* b = bufs[idx]; idx = (idx + 1) % 4;
    va_list ap; va_start(ap, fmt); std::vsnprintf(b, 1024, fmt, ap); va_end(ap);
    return b;
}
Ray GetScreenToWorldRay(Vector2 p, Camera3D cam) { return GetScreenToWorldRayEx(p, cam, GetScreenWidth(), GetScreenHeight()); }
Vector2 GetWorldToScreen(Vector3 p, Camera3D cam) { return GetWorldToScreenEx(p, cam, GetScreenWidth(), GetScreenHeight()); }

// ================= 3D =================
static void beginMode3D(Camera3D cam, bool overlay) {
    State& s = S();
    float aspect = s.win.height() ? float(s.win.width()) / float(s.win.height()) : 1.0f;
    Matrix proj;
    if (cam.projection == CAMERA_PERSPECTIVE) {
        proj = MatrixPerspective(cam.fovy * DEG2RAD, aspect, 0.01, 1000.0);
    } else {
        double top = cam.fovy / 2.0, right = top * aspect;
        proj = MatrixOrtho(-right, right, -top, top, 0.01, 1000.0);
    }
    Matrix view = MatrixLookAt(cam.position, cam.target, cam.up);
    s.view = view; s.proj = proj; s.in3D = true;
    float16 vp = MatrixToFloatV(MatrixMultiply(view, proj));
    s.rend.begin3D(vp.v, cam.position.x, cam.position.y, cam.position.z, overlay);
}
void BeginMode3D(Camera3D cam) { beginMode3D(cam, false); }
void BeginMode3DOverlay(Camera3D cam) { beginMode3D(cam, true); }
void EndMode3D() { S().rend.end3D(); S().in3D = false; }

void DrawLine3D(Vector3 a, Vector3 b, Color c) {
    S().rend.line(V3(a, {}, c), V3(b, {}, c));
}
void DrawTriangle3D(Vector3 a, Vector3 b, Vector3 c, Color col) {
    Vector3 n = faceNormal(a, b, c);
    S().rend.tri(V3(a, n, col), V3(b, n, col), V3(c, n, col));
}

static void quad(Vector3 a, Vector3 b, Vector3 c, Vector3 d, Vector3 n, Color col) {
    auto& r = S().rend;
    r.tri(V3(a, n, col), V3(b, n, col), V3(c, n, col));
    r.tri(V3(a, n, col), V3(c, n, col), V3(d, n, col));
}
void DrawCube(Vector3 p, float w, float h, float l, Color c) {
    float x = w / 2, y = h / 2, z = l / 2;
    Vector3 v[8];
    for (int i = 0; i < 8; ++i) v[i] = {p.x + ((i & 1) ? x : -x), p.y + ((i & 2) ? y : -y), p.z + ((i & 4) ? z : -z)};
    quad(v[1], v[3], v[7], v[5], {1, 0, 0}, c);    // +X
    quad(v[0], v[4], v[6], v[2], {-1, 0, 0}, c);   // -X
    quad(v[2], v[6], v[7], v[3], {0, 1, 0}, c);    // +Y
    quad(v[0], v[1], v[5], v[4], {0, -1, 0}, c);   // -Y
    quad(v[4], v[5], v[7], v[6], {0, 0, 1}, c);    // +Z
    quad(v[0], v[2], v[3], v[1], {0, 0, -1}, c);   // -Z
}
void DrawCubeV(Vector3 p, Vector3 s, Color c) { DrawCube(p, s.x, s.y, s.z, c); }
void DrawCubeWires(Vector3 p, float w, float h, float l, Color c) {
    float x = w / 2, y = h / 2, z = l / 2;
    Vector3 v[8];
    for (int i = 0; i < 8; ++i) v[i] = {p.x + ((i & 1) ? x : -x), p.y + ((i & 2) ? y : -y), p.z + ((i & 4) ? z : -z)};
    for (int i = 0; i < 8; ++i)
        for (int b = 1; b < 8; b <<= 1)
            if (!(i & b)) DrawLine3D(v[i], v[i | b], c);
}

static void sphereGeom(Vector3 ctr, float r, int rings, int slices, Color col, bool wires) {
    auto pt = [&](int i, int j) {
        float phi = PI * float(i) / float(rings), th = 2 * PI * float(j) / float(slices);
        Vector3 n{std::sin(phi) * std::cos(th), std::cos(phi), std::sin(phi) * std::sin(th)};
        return n;
    };
    auto P = [&](Vector3 n) { return Vector3{ctr.x + n.x * r, ctr.y + n.y * r, ctr.z + n.z * r}; };
    for (int i = 0; i < rings; ++i)
        for (int j = 0; j < slices; ++j) {
            Vector3 a = pt(i, j), b = pt(i + 1, j), c = pt(i + 1, j + 1), d = pt(i, j + 1);
            if (wires) {
                DrawLine3D(P(a), P(b), col);
                DrawLine3D(P(a), P(d), col);
            } else {
                auto& rd = S().rend;
                rd.tri(V3(P(a), a, col), V3(P(c), c, col), V3(P(b), b, col));
                rd.tri(V3(P(a), a, col), V3(P(d), d, col), V3(P(c), c, col));
            }
        }
}
void DrawSphere(Vector3 c, float r, Color col) { sphereGeom(c, r, 16, 16, col, false); }
void DrawSphereWires(Vector3 c, float r, int rings, int slices, Color col) { sphereGeom(c, r, rings, slices, col, true); }
void DrawPlane(Vector3 c, Vector2 s, Color col) {
    float x = s.x / 2, z = s.y / 2;
    quad({c.x - x, c.y, c.z - z}, {c.x - x, c.y, c.z + z}, {c.x + x, c.y, c.z + z}, {c.x + x, c.y, c.z - z}, {0, 1, 0}, col);
}
void DrawGrid(int slices, float spacing) {
    int half = slices / 2;
    for (int i = -half; i <= half; ++i) {
        Color c = (i == 0) ? Color{128, 128, 128, 255} : Color{200, 200, 200, 255};
        DrawLine3D({float(i) * spacing, 0, float(-half) * spacing}, {float(i) * spacing, 0, float(half) * spacing}, c);
        DrawLine3D({float(-half) * spacing, 0, float(i) * spacing}, {float(half) * spacing, 0, float(i) * spacing}, c);
    }
}

// Interleaved vertices (indices expanded) in raylib's attribute order.
static std::vector<Vertex3D> packMesh(const Mesh& m) {
    const int tris = m.indices ? m.triangleCount : m.vertexCount / 3;
    std::vector<Vertex3D> verts(size_t(tris) * 3);
    for (int k = 0; k < tris * 3; ++k) {
        int i = m.indices ? m.indices[k] : k;
        Vertex3D v{};
        v.x = m.vertices[i * 3]; v.y = m.vertices[i * 3 + 1]; v.z = m.vertices[i * 3 + 2];
        if (m.texcoords) { v.u = m.texcoords[i * 2]; v.v = m.texcoords[i * 2 + 1]; }
        if (m.normals) { v.nx = m.normals[i * 3]; v.ny = m.normals[i * 3 + 1]; v.nz = m.normals[i * 3 + 2]; }
        if (m.colors) { v.r = m.colors[i * 4]; v.g = m.colors[i * 4 + 1]; v.b = m.colors[i * 4 + 2]; v.a = m.colors[i * 4 + 3]; }
        else v.r = v.g = v.b = v.a = 255;
        verts[size_t(k)] = v;
    }
    return verts;
}

static void drawMeshCustom(const Mesh& m, const Material& mat, Matrix t) {
    State& s = S();
    render::StdUniforms u{};
    toFloats(t, u.model); toFloats(s.view, u.view); toFloats(s.proj, u.proj);
    toFloats(MatrixMultiply(MatrixMultiply(t, s.view), s.proj), u.mvp);
    toFloats(transposeM(MatrixInvert(t)), u.normal);
    Color tint = mat.maps ? mat.maps[MATERIAL_MAP_ALBEDO].color : WHITE;
    u.diffuse[0] = tint.r / 255.0f; u.diffuse[1] = tint.g / 255.0f; u.diffuse[2] = tint.b / 255.0f; u.diffuse[3] = tint.a / 255.0f;
    const int tex = mat.maps ? int(mat.maps[MATERIAL_MAP_ALBEDO].texture.id) : 0;
    if (m.vaoId) {   // resident on the GPU: no per-draw vertex work
        s.rend.drawCustomMesh(int(mat.shader.id), int(m.vaoId), render::Topo::Triangles, u, tex);
        return;
    }
    static std::vector<Vertex3D> verts;
    verts = packMesh(m);
    s.rend.drawCustom(int(mat.shader.id), verts.data(), uint32_t(verts.size()), render::Topo::Triangles, u, tex);
}

void DrawMesh(Mesh m, Material mat, Matrix t) {
    if (!m.vertices || m.vertexCount <= 0) return;
    if (mat.shader.id != 0) { drawMeshCustom(m, mat, t); return; }
    Color tint = (mat.maps ? mat.maps[MATERIAL_MAP_ALBEDO].color : WHITE);
    // Normal matrix = cofactors of the upper 3x3 (inverse-transpose up to scale).
    float a = t.m0, b = t.m4, c = t.m8, d = t.m1, e = t.m5, f = t.m9, g = t.m2, h = t.m6, i = t.m10;
    float nm[9] = {e * i - f * h, f * g - d * i, d * h - e * g,
                   c * h - b * i, a * i - c * g, b * g - a * h,
                   b * f - c * e, c * d - a * f, a * e - b * d};
    auto xf = [&](int idx) {
        Vector3 p = Vector3Transform({m.vertices[idx * 3], m.vertices[idx * 3 + 1], m.vertices[idx * 3 + 2]}, t);
        Vector3 n{};
        if (m.normals) {
            float x = m.normals[idx * 3], y = m.normals[idx * 3 + 1], z = m.normals[idx * 3 + 2];
            n = Vector3Normalize({nm[0] * x + nm[3] * y + nm[6] * z, nm[1] * x + nm[4] * y + nm[7] * z,
                                  nm[2] * x + nm[5] * y + nm[8] * z});
        }
        Color col = tint;
        if (m.colors) {
            col.r = (unsigned char)(tint.r * m.colors[idx * 4] / 255);   col.g = (unsigned char)(tint.g * m.colors[idx * 4 + 1] / 255);
            col.b = (unsigned char)(tint.b * m.colors[idx * 4 + 2] / 255); col.a = (unsigned char)(tint.a * m.colors[idx * 4 + 3] / 255);
        }
        return V3(p, n, col);
    };
    int tris = m.indices ? m.triangleCount : m.vertexCount / 3;
    for (int k = 0; k < tris; ++k) {
        int i0 = m.indices ? m.indices[k * 3] : k * 3, i1 = m.indices ? m.indices[k * 3 + 1] : k * 3 + 1,
            i2 = m.indices ? m.indices[k * 3 + 2] : k * 3 + 2;
        Vertex3D v0 = xf(i0), v1 = xf(i1), v2 = xf(i2);
        if (!m.normals) {   // flat shading fallback
            Vector3 n = faceNormal({v0.x, v0.y, v0.z}, {v1.x, v1.y, v1.z}, {v2.x, v2.y, v2.z});
            v0.nx = v1.nx = v2.nx = n.x; v0.ny = v1.ny = v2.ny = n.y; v0.nz = v1.nz = v2.nz = n.z;
        }
        S().rend.tri(v0, v1, v2);
    }
}

// ---- mesh generation (unindexed triangle lists with normals) ----
namespace {
struct MeshBuilder {
    std::vector<float> pos, nrm, uv;
    void tri(Vector3 a, Vector3 b, Vector3 c, Vector3 na, Vector3 nb, Vector3 nc) {
        for (auto [p, n] : {std::pair{a, na}, std::pair{b, nb}, std::pair{c, nc}}) {
            pos.insert(pos.end(), {p.x, p.y, p.z}); nrm.insert(nrm.end(), {n.x, n.y, n.z}); uv.insert(uv.end(), {0, 0});
        }
    }
    void tri(Vector3 a, Vector3 b, Vector3 c) { Vector3 n = faceNormal(a, b, c); tri(a, b, c, n, n, n); }
    void quad(Vector3 a, Vector3 b, Vector3 c, Vector3 d) { tri(a, b, c); tri(a, c, d); }
    Mesh finish() {
        Mesh m{};
        m.vertexCount = int(pos.size() / 3); m.triangleCount = m.vertexCount / 3;
        m.vertices = static_cast<float*>(std::malloc(pos.size() * 4)); std::memcpy(m.vertices, pos.data(), pos.size() * 4);
        m.normals = static_cast<float*>(std::malloc(nrm.size() * 4)); std::memcpy(m.normals, nrm.data(), nrm.size() * 4);
        m.texcoords = static_cast<float*>(std::malloc(uv.size() * 4)); std::memcpy(m.texcoords, uv.data(), uv.size() * 4);
        UploadMesh(&m, false);
        return m;
    }
};
}
Mesh GenMeshPlane(float w, float l, int rx, int rz) {
    MeshBuilder b; rx = std::max(rx, 1); rz = std::max(rz, 1);
    for (int i = 0; i < rx; ++i) for (int j = 0; j < rz; ++j) {
        float x0 = -w / 2 + w * i / rx, x1 = -w / 2 + w * (i + 1) / rx, z0 = -l / 2 + l * j / rz, z1 = -l / 2 + l * (j + 1) / rz;
        b.tri({x0, 0, z0}, {x0, 0, z1}, {x1, 0, z1}, {0, 1, 0}, {0, 1, 0}, {0, 1, 0});
        b.tri({x0, 0, z0}, {x1, 0, z1}, {x1, 0, z0}, {0, 1, 0}, {0, 1, 0}, {0, 1, 0});
    }
    return b.finish();
}
Mesh GenMeshCube(float w, float h, float l) {
    MeshBuilder b; float x = w / 2, y = h / 2, z = l / 2;
    Vector3 v[8];
    for (int i = 0; i < 8; ++i) v[i] = {(i & 1) ? x : -x, (i & 2) ? y : -y, (i & 4) ? z : -z};
    b.quad(v[1], v[3], v[7], v[5]); b.quad(v[0], v[4], v[6], v[2]); b.quad(v[2], v[6], v[7], v[3]);
    b.quad(v[0], v[1], v[5], v[4]); b.quad(v[4], v[5], v[7], v[6]); b.quad(v[0], v[2], v[3], v[1]);
    return b.finish();
}
Mesh GenMeshSphere(float r, int rings, int slices) {
    MeshBuilder b; rings = std::max(rings, 2); slices = std::max(slices, 3);
    auto pt = [&](int i, int j) {
        float phi = PI * i / rings, th = 2 * PI * j / slices;
        return Vector3{std::sin(phi) * std::cos(th), std::cos(phi), std::sin(phi) * std::sin(th)};
    };
    for (int i = 0; i < rings; ++i) for (int j = 0; j < slices; ++j) {
        Vector3 a = pt(i, j), bb = pt(i + 1, j), c = pt(i + 1, j + 1), d = pt(i, j + 1);
        b.tri(Vector3Scale(a, r), Vector3Scale(c, r), Vector3Scale(bb, r), a, c, bb);   // counter-clockwise seen from outside
        b.tri(Vector3Scale(a, r), Vector3Scale(d, r), Vector3Scale(c, r), a, d, c);
    }
    return b.finish();
}
static Mesh coneOrCylinder(float r, float h, int slices, bool cone) {
    MeshBuilder b; slices = std::max(slices, 3);
    for (int j = 0; j < slices; ++j) {
        float a0 = 2 * PI * j / slices, a1 = 2 * PI * (j + 1) / slices;
        Vector3 b0{r * std::cos(a0), 0, r * std::sin(a0)}, b1{r * std::cos(a1), 0, r * std::sin(a1)};
        Vector3 n0{std::cos(a0), 0, std::sin(a0)}, n1{std::cos(a1), 0, std::sin(a1)};
        Vector3 apex{0, h, 0}, t0{b0.x, h, b0.z}, t1{b1.x, h, b1.z};
        if (cone) {
            b.tri(b0, apex, b1, n0, Vector3Normalize({n0.x + n1.x, r / h, n0.z + n1.z}), n1);
        } else {
            b.tri(b0, t0, t1, n0, n0, n1); b.tri(b0, t1, b1, n0, n1, n1);
            b.tri({0, h, 0}, t1, t0, {0, 1, 0}, {0, 1, 0}, {0, 1, 0});
        }
        b.tri({0, 0, 0}, b0, b1, {0, -1, 0}, {0, -1, 0}, {0, -1, 0});
    }
    return b.finish();
}
Mesh GenMeshCylinder(float r, float h, int slices) { return coneOrCylinder(r, h, slices, false); }
Mesh GenMeshCone(float r, float h, int slices) { return coneOrCylinder(r, h, slices, true); }
void UploadMesh(Mesh* m, bool dynamic) {
    if (!m || !m->vertices || m->vertexCount <= 0 || !S().ready) return;   // before InitWindow: stays CPU-side
    std::vector<Vertex3D> v = packMesh(*m);
    m->vaoId = unsigned(S().rend.createMesh(v.data(), uint32_t(v.size()), dynamic));
}
void UpdateMeshBuffer(Mesh m, int index, const void* data, int size, int offset) {
    if (m.vaoId && S().ready) S().rend.updateMeshAttribute(int(m.vaoId), index, data, size_t(size), size_t(offset));
}
void UnloadMesh(Mesh m) {
    if (m.vaoId && S().ready) S().rend.destroyMesh(int(m.vaoId));
    std::free(m.vertices); std::free(m.texcoords); std::free(m.texcoords2); std::free(m.normals);
    std::free(m.tangents); std::free(m.colors); std::free(m.indices); std::free(m.animVertices);
    std::free(m.animNormals); std::free(m.boneIds); std::free(m.boneWeights); std::free(m.boneMatrices);
    std::free(m.vboId);
}
Material LoadMaterialDefault() {
    Material m{};
    m.maps = static_cast<MaterialMap*>(std::calloc(12, sizeof(MaterialMap)));
    m.maps[MATERIAL_MAP_ALBEDO].color = WHITE;
    return m;
}
void UnloadMaterial(Material m) { std::free(m.maps); }

// ================= 2D =================
namespace {
#include "render/FontData.inc"
}
void DrawTriangle(Vector2 a, Vector2 b, Vector2 c, Color col) {
    S().rend.tri2D(V2(a.x, a.y, col), V2(b.x, b.y, col), V2(c.x, c.y, col));
}
void DrawRectangle(int x, int y, int w, int h, Color c) {
    if (S().activeShader) { rectCustom(float(x), float(y), float(w), float(h), c); return; }
    auto& r = S().rend; float x0 = float(x), y0 = float(y), x1 = float(x + w), y1 = float(y + h);
    r.tri2D(V2(x0, y0, c), V2(x1, y0, c), V2(x1, y1, c));
    r.tri2D(V2(x0, y0, c), V2(x1, y1, c), V2(x0, y1, c));
}
void DrawRectangleV(Vector2 p, Vector2 s, Color c) {
    if (S().activeShader) { rectCustom(p.x, p.y, s.x, s.y, c); return; }
    auto& r = S().rend;
    r.tri2D(V2(p.x, p.y, c), V2(p.x + s.x, p.y, c), V2(p.x + s.x, p.y + s.y, c));
    r.tri2D(V2(p.x, p.y, c), V2(p.x + s.x, p.y + s.y, c), V2(p.x, p.y + s.y, c));
}
void DrawRectangleRec(Rectangle r, Color c) { DrawRectangleV({r.x, r.y}, {r.width, r.height}, c); }
void DrawRectangleRounded(Rectangle rc, float roundness, int segments, Color c) {
    float rad = std::min(rc.width, rc.height) * 0.5f * std::min(std::max(roundness, 0.0f), 1.0f);
    if (rad <= 0.5f) { DrawRectangleRec(rc, c); return; }
    int seg = std::max(segments / 4, 3);
    std::vector<Vector2> pts;
    Vector2 centers[4] = {{rc.x + rc.width - rad, rc.y + rad}, {rc.x + rc.width - rad, rc.y + rc.height - rad},
                          {rc.x + rad, rc.y + rc.height - rad}, {rc.x + rad, rc.y + rad}};
    for (int k = 0; k < 4; ++k)
        for (int i = 0; i <= seg; ++i) {
            float a = (-90.0f + 90.0f * k + 90.0f * i / seg) * DEG2RAD;
            pts.push_back({centers[k].x + std::cos(a) * rad, centers[k].y + std::sin(a) * rad});
        }
    Vector2 mid{rc.x + rc.width / 2, rc.y + rc.height / 2};
    for (size_t i = 0; i < pts.size(); ++i) DrawTriangle(mid, pts[i], pts[(i + 1) % pts.size()], c);
}
void DrawRectangleLinesEx(Rectangle r, float t, Color c) {
    DrawRectangleRec({r.x, r.y, r.width, t}, c);
    DrawRectangleRec({r.x, r.y + r.height - t, r.width, t}, c);
    DrawRectangleRec({r.x, r.y + t, t, r.height - 2 * t}, c);
    DrawRectangleRec({r.x + r.width - t, r.y + t, t, r.height - 2 * t}, c);
}
void DrawRectangleGradientV(int x, int y, int w, int h, Color top, Color bot) {
    auto& r = S().rend; float x0 = float(x), y0 = float(y), x1 = float(x + w), y1 = float(y + h);
    r.tri2D(V2(x0, y0, top), V2(x1, y0, top), V2(x1, y1, bot));
    r.tri2D(V2(x0, y0, top), V2(x1, y1, bot), V2(x0, y1, bot));
}
void DrawRectangleGradientH(int x, int y, int w, int h, Color left, Color right) {
    auto& r = S().rend; float x0 = float(x), y0 = float(y), x1 = float(x + w), y1 = float(y + h);
    r.tri2D(V2(x0, y0, left), V2(x1, y0, right), V2(x1, y1, right));
    r.tri2D(V2(x0, y0, left), V2(x1, y1, right), V2(x0, y1, left));
}
void DrawText(const char* text, int x, int y, int fontSize, Color c) {
    if (!text) return;
    const float sc = float(fontSize) / 12.0f;
    auto px = [](float v) { return int(std::floor(v + 0.5f)); };
    float cx = float(x), cy = float(y);
    for (const char* p = text; *p; ++p) {
        if (*p == '\n') { cx = float(x); cy += float(kFontH) * sc; continue; }
        unsigned char ch = static_cast<unsigned char>(*p);
        if (ch < 32 || ch > 126) ch = '?';
        const unsigned short* rows = kFontRows[ch - 32];
        for (int r = 0; r < kFontH; ++r) {
            unsigned bits = rows[r];
            for (int b = 0; b < kFontW;) {
                if (!((bits >> (kFontW - 1 - b)) & 1)) { ++b; continue; }
                int e = b; while (e < kFontW && ((bits >> (kFontW - 1 - e)) & 1)) ++e;
                int x0 = px(cx + float(b) * sc), x1 = px(cx + float(e) * sc);
                int y0 = px(cy + float(r) * sc), y1 = px(cy + float(r + 1) * sc);
                if (x1 > x0 && y1 > y0) DrawRectangle(x0, y0, x1 - x0, y1 - y0, c);
                b = e;
            }
        }
        cx += float(kFontW) * sc;
    }
}
int MeasureText(const char* text, int fontSize) {
    if (!text) return 0;
    int best = 0, cur = 0;
    for (const char* p = text; *p; ++p) { if (*p == '\n') { best = std::max(best, cur); cur = 0; } else ++cur; }
    best = std::max(best, cur);
    return int(std::lround(float(best * kFontW) * float(fontSize) / 12.0f));
}
void DrawLineEx(Vector2 a, Vector2 b, float th, Color c) {
    Vector2 d = Vector2Subtract(b, a); float len = Vector2Length(d);
    if (len < 1e-6f) return;
    Vector2 n{-d.y / len * th * 0.5f, d.x / len * th * 0.5f};
    auto& r = S().rend;
    r.tri2D(V2(a.x + n.x, a.y + n.y, c), V2(b.x + n.x, b.y + n.y, c), V2(b.x - n.x, b.y - n.y, c));
    r.tri2D(V2(a.x + n.x, a.y + n.y, c), V2(b.x - n.x, b.y - n.y, c), V2(a.x - n.x, a.y - n.y, c));
}
void DrawLineV(Vector2 a, Vector2 b, Color c) { DrawLineEx(a, b, 1.0f, c); }
void DrawLine(int x1, int y1, int x2, int y2, Color c) { DrawLineEx({float(x1), float(y1)}, {float(x2), float(y2)}, 1.0f, c); }
void DrawCircleV(Vector2 c, float r, Color col) {
    int n = std::max(12, int(r * 0.9f) + 12);
    for (int i = 0; i < n; ++i) {
        float a0 = 2 * PI * i / n, a1 = 2 * PI * (i + 1) / n;
        DrawTriangle(c, {c.x + std::cos(a0) * r, c.y + std::sin(a0) * r}, {c.x + std::cos(a1) * r, c.y + std::sin(a1) * r}, col);
    }
}
void DrawCircle(int cx, int cy, float r, Color c) { DrawCircleV({float(cx), float(cy)}, r, c); }
void DrawCircleLinesV(Vector2 c, float r, Color col) {
    int n = std::max(16, int(r * 0.9f) + 16);
    for (int i = 0; i < n; ++i) {
        float a0 = 2 * PI * i / n, a1 = 2 * PI * (i + 1) / n;
        DrawLineEx({c.x + std::cos(a0) * r, c.y + std::sin(a0) * r}, {c.x + std::cos(a1) * r, c.y + std::sin(a1) * r}, 1.0f, col);
    }
}
void DrawCircleLines(int cx, int cy, float r, Color c) { DrawCircleLinesV({float(cx), float(cy)}, r, c); }

// ================= shaders =================
Shader LoadShaderFromMemory(const char* vs, const char* fs) {
    State& s = S();
    Shader sh{0, static_cast<int*>(std::malloc(32 * sizeof(int)))};
    for (int i = 0; i < 32; ++i) sh.locs[i] = -1;
    const render::PortedShader* p = render::findPortedShader(vs, fs);
    if (!p) {
        std::fprintf(stderr, "LoadShaderFromMemory: this shader source pair is not precompiled.\n"
                             "  Add its GLSL to shaders/<set>/<name>.{vs,fs}.glsl and run tools/shaders.py build.\n");
        return sh;
    }
    sh.id = unsigned(s.rend.createShader(*p));
    return sh;
}
bool IsShaderReady(Shader sh) { return sh.id != 0; }
int GetShaderLocation(Shader sh, const char* name) { return S().rend.uniformIndex(int(sh.id), name); }
void UnloadShader(Shader sh) { if (sh.id) S().rend.destroyShader(int(sh.id)); std::free(sh.locs); }

static size_t uniformBytes(int type) {
    switch (type) {
        case SHADER_UNIFORM_FLOAT: case SHADER_UNIFORM_INT: return 4;
        case SHADER_UNIFORM_VEC2: case SHADER_UNIFORM_IVEC2: return 8;
        case SHADER_UNIFORM_VEC3: case SHADER_UNIFORM_IVEC3: return 12;
        case SHADER_UNIFORM_VEC4: case SHADER_UNIFORM_IVEC4: return 16;
        default: return 0;
    }
}
void SetShaderValueV(Shader sh, int loc, const void* value, int type, int count) {
    auto* info = S().rend.uniformInfo(int(sh.id), loc);
    uint8_t* block = S().rend.shaderBlock(int(sh.id));
    size_t elem = uniformBytes(type);
    if (!info || !block || !elem || !value) return;
    // std140: array elements are 16-byte strided (a single value is packed)
    int n = std::min(count, int(info->count));
    for (int i = 0; i < n; ++i)
        std::memcpy(block + info->offset + (info->count > 1 ? size_t(i) * 16 : 0), static_cast<const uint8_t*>(value) + size_t(i) * elem, elem);
}
void SetShaderValue(Shader sh, int loc, const void* value, int type) { SetShaderValueV(sh, loc, value, type, 1); }
void SetShaderValueMatrix(Shader sh, int loc, Matrix mat) {
    auto* info = S().rend.uniformInfo(int(sh.id), loc);
    uint8_t* block = S().rend.shaderBlock(int(sh.id));
    if (info && block) toFloats(mat, reinterpret_cast<float*>(block + info->offset));
}
void BeginShaderMode(Shader sh) { S().activeShader = int(sh.id); }
void EndShaderMode() { S().activeShader = 0; }

// ================= images / textures =================
Image GenImageColor(int w, int h, Color c) {
    Image im{std::malloc(size_t(w) * size_t(h) * 4), w, h, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
    auto* p = static_cast<unsigned char*>(im.data);
    for (int i = 0; i < w * h; ++i) { p[i * 4] = c.r; p[i * 4 + 1] = c.g; p[i * 4 + 2] = c.b; p[i * 4 + 3] = c.a; }
    return im;
}
static void putPixel(Image* im, int x, int y, Color c) {
    if (x < 0 || y < 0 || x >= im->width || y >= im->height) return;
    auto* p = static_cast<unsigned char*>(im->data) + (size_t(y) * size_t(im->width) + size_t(x)) * 4;
    p[0] = c.r; p[1] = c.g; p[2] = c.b; p[3] = c.a;
}
void ImageDrawRectangle(Image* im, int x, int y, int w, int h, Color c) {
    if (!im || !im->data) return;
    for (int j = y; j < y + h; ++j) for (int i = x; i < x + w; ++i) putPixel(im, i, j, c);
}
void ImageDrawCircle(Image* im, int cx, int cy, int r, Color c) {
    if (!im || !im->data) return;
    for (int j = -r; j <= r; ++j) for (int i = -r; i <= r; ++i) if (i * i + j * j <= r * r) putPixel(im, cx + i, cy + j, c);
}
void UnloadImage(Image im) { std::free(im.data); }
Texture2D LoadTextureFromImage(Image im) {
    int id = S().rend.createTexture(im.width, im.height, im.data, false);
    return {unsigned(id), im.width, im.height, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
}
void GenTextureMipmaps(Texture2D* t) { if (t && t->id) { S().rend.generateMipmaps(int(t->id)); t->mipmaps = 2; } }
void SetTextureFilter(Texture2D t, int f) { if (t.id) S().rend.setTextureFilter(int(t.id), f); }
void SetTextureWrap(Texture2D t, int w) { if (t.id) S().rend.setTextureWrap(int(t.id), w); }
void UnloadTexture(Texture2D t) { if (t.id) S().rend.destroyTexture(int(t.id)); }

void DrawTexturePro(Texture2D tex, Rectangle src, Rectangle dst, Vector2 origin, float rotation, Color tint) {
    if (!tex.id || tex.width <= 0 || tex.height <= 0) return;
    const float tw = float(tex.width), th = float(tex.height);
    bool flipX = false;
    if (src.width < 0) { flipX = true; src.width = -src.width; }
    if (src.height < 0) src.y -= src.height;
    Vector2 tl, tr, bl, br;
    if (rotation == 0.0f) {
        float x = dst.x - origin.x, y = dst.y - origin.y;
        tl = {x, y}; tr = {x + dst.width, y}; bl = {x, y + dst.height}; br = {x + dst.width, y + dst.height};
    } else {
        float sr = std::sin(rotation * DEG2RAD), cr = std::cos(rotation * DEG2RAD);
        auto at = [&](float dx, float dy) { return Vector2{dst.x + dx * cr - dy * sr, dst.y + dx * sr + dy * cr}; };
        tl = at(-origin.x, -origin.y); tr = at(-origin.x + dst.width, -origin.y);
        bl = at(-origin.x, -origin.y + dst.height); br = at(-origin.x + dst.width, -origin.y + dst.height);
    }
    float uL = flipX ? (src.x + src.width) / tw : src.x / tw, uR = flipX ? src.x / tw : (src.x + src.width) / tw;
    float vT = src.y / th, vB = (src.y + src.height) / th;
    Vector2 p[4] = {tl, bl, br, tr}, uv[4] = {{uL, vT}, {uL, vB}, {uR, vB}, {uR, vT}};
    State& s = S();
    int shader = s.activeShader ? s.activeShader : defaultShaderId();
    quad2DCustom(shader, int(tex.id), p, uv, tint);
}

// ================= render state =================
void rlEnableDepthMask() { S().rend.setDepthWrite(true); }
void rlDisableDepthMask() { S().rend.setDepthWrite(false); }
void rlEnableDepthTest() { S().rend.setDepthTest(true); }
void rlDisableDepthTest() { S().rend.setDepthTest(false); }
void rlEnableBackfaceCulling() { S().rend.setCullBack(true); }
void rlDisableBackfaceCulling() { S().rend.setCullBack(false); }
void rlDrawRenderBatchActive() {}
void BeginBlendMode(int mode) {
    S().rend.setBlend(mode == BLEND_ADDITIVE ? render::Blend::Additive : mode == BLEND_MULTIPLIED ? render::Blend::Multiplied : render::Blend::Alpha);
}
void EndBlendMode() { S().rend.setBlend(render::Blend::Alpha); }
void BeginScissorMode(int x, int y, int w, int h) { S().rend.setScissor(true, x, y, w, h); }
void EndScissorMode() { S().rend.setScissor(false, 0, 0, 0, 0); }

// ================= lighting extensions =================
void SetSunLight(Vector3 d, Color c, float k) { S().rend.setSun(d.x, d.y, d.z, c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, k); }
void SetAmbientLight(Color sky, Color gnd) {
    auto lin = [](unsigned char v) { return std::pow(v / 255.0f, 2.2f); };
    float s[3] = {lin(sky.r), lin(sky.g), lin(sky.b)}, g[3] = {lin(gnd.r), lin(gnd.g), lin(gnd.b)};
    S().rend.setAmbient(s, g);
}
void SetShadowBounds(Vector3 c, float r) { S().rend.setShadows(S().rend.shadowsEnabled(), c.x, c.y, c.z, r); }
void EnableShadows(bool on) { S().rend.setShadowsEnabled(on); }
bool AddPointLight(Vector3 p, float range, Color c, float k) {
    auto lin = [](unsigned char v) { return std::pow(v / 255.0f, 2.2f); };
    return S().rend.addPointLight(p.x, p.y, p.z, range, lin(c.r), lin(c.g), lin(c.b), k);
}

} // namespace foundry::compat
