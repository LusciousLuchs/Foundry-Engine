#include "scene/SceneRender.hpp"
#include <cmath>
#include <cstdio>

namespace foundry::scene {
using namespace foundry::compat;

SceneRenderer::SceneRenderer() {
    cube_ = GenMeshCube(1, 1, 1);
    sphere_ = GenMeshSphere(0.5f, 24, 32);
    cylinder_ = GenMeshCylinder(0.5f, 1.0f, 32);   // base at y=0; recentred in drawEntities
    cone_ = GenMeshCone(0.5f, 1.0f, 32);
    plane_ = GenMeshPlane(1, 1, 1, 1);
    mat_ = LoadMaterialDefault();
}

SceneRenderer::~SceneRenderer() {
    UnloadMesh(cube_); UnloadMesh(sphere_); UnloadMesh(cylinder_); UnloadMesh(cone_); UnloadMesh(plane_);
    UnloadMaterial(mat_);
}

void SceneRenderer::applyEnvironment(const Scene& s) {
    const Environment& e = s.env;
    ClearBackground(e.background);
    SetSunLight(e.sunDir, e.sunColor, e.sunIntensity);
    SetAmbientLight(e.sky, e.ground);
    EnableShadows(e.shadows);
    SetShadowBounds(e.shadowCenter, e.shadowRadius);
    int n = 0;
    for (const Entity& l : s.entities)
        if (l.kind == Kind::Light && l.visible && AddPointLight(l.pos, l.range, l.color, l.intensity)) ++n;
}

void SceneRenderer::drawEntities(const Scene& s, bool markers) {
    for (const Entity& e : s.entities) {
        if (!e.visible) continue;
        if (e.kind == Kind::Light) {
            if (markers) DrawSphereWires(e.pos, 0.3f, 6, 10, e.color);
            continue;
        }
        Matrix m = Scene::modelMatrix(e);
        const Mesh* mesh = &cube_;
        switch (e.kind) {
            case Kind::Sphere: mesh = &sphere_; break;
            case Kind::Cylinder: mesh = &cylinder_; m = MatrixMultiply(MatrixTranslate(0, -0.5f, 0), m); break;
            case Kind::Cone: mesh = &cone_; m = MatrixMultiply(MatrixTranslate(0, -0.5f, 0), m); break;
            case Kind::Plane: mesh = &plane_; break;
            default: break;
        }
        mat_.maps[MATERIAL_MAP_ALBEDO].color = e.color;
        DrawMesh(*mesh, mat_, m);
    }
}

void SceneRenderer::drawOutline(const Entity& e, Color c) {
    if (e.kind == Kind::Light) { DrawSphereWires(e.pos, 0.42f, 8, 12, c); return; }
    Matrix m = Scene::modelMatrix(e);
    Vector3 v[8];
    float h = e.kind == Kind::Plane ? 0.0f : 0.5f;
    for (int i = 0; i < 8; ++i)
        v[i] = Vector3Transform({(i & 1) ? 0.5f : -0.5f, (i & 2) ? h : -h, (i & 4) ? 0.5f : -0.5f}, m);
    for (int i = 0; i < 8; ++i)
        for (int b = 1; b < 8; b <<= 1)
            if (!(i & b)) DrawLine3D(v[i], v[i | b], c);
}

void SceneRenderer::drawGrid(float half, float step) {
    const float y = 0.004f;   // just above y=0 so floors do not z-fight with the lines
    for (float t = -half; t <= half + 1e-3f; t += step) {
        bool axis = std::fabs(t) < 1e-3f;
        Color c = axis ? Color{120, 120, 130, 255} : Color{70, 74, 86, 255};
        if (!axis) { DrawLine3D({t, y, -half}, {t, y, half}, c); DrawLine3D({-half, y, t}, {half, y, t}, c); }
    }
    DrawLine3D({-half, y, 0}, {half, y, 0}, {200, 70, 70, 255});     // X axis
    DrawLine3D({0, y, -half}, {0, y, half}, {70, 110, 220, 255});    // Z axis
    DrawLine3D({0, 0, 0}, {0, half * 0.5f, 0}, {90, 200, 90, 255}); // Y axis
}

} // namespace foundry::scene
