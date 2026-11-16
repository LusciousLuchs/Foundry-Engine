// Windows-only checks that do not need a window or GPU (run under Wine in CI).
#include "compat/raylib.hpp"
#include "render/Renderer.hpp"
#include <cstdio>
#include <cstring>
#include <cmath>

using namespace foundry::compat;
static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

// Every triangle must wind counter-clockwise seen from outside (raylib's front face), i.e. its geometric
// normal agrees with the stored normal; back-face culling relies on it.
static int badWinding(const Mesh& m, bool outwardFromOrigin) {
    int bad = 0;
    for (int t = 0; t < m.vertexCount / 3; ++t) {
        const float* p = m.vertices + t * 9;
        Vector3 a{p[0], p[1], p[2]}, b{p[3], p[4], p[5]}, c{p[6], p[7], p[8]};
        Vector3 g = Vector3CrossProduct(Vector3Subtract(b, a), Vector3Subtract(c, a));
        if (Vector3Length(g) < 1e-5f) continue;   // slivers at the poles: sign is rounding noise
        const float* n = m.normals + t * 9;
        Vector3 sn{n[0] + n[3] + n[6], n[1] + n[4] + n[7], n[2] + n[5] + n[8]};
        if (Vector3DotProduct(g, sn) <= 0) ++bad;
    }
    (void)outwardFromOrigin;
    return bad;
}

int main() {
    Mesh cube = GenMeshCube(1, 2, 3), sph = GenMeshSphere(1, 12, 16), cyl = GenMeshCylinder(1, 2, 20), cone = GenMeshCone(1, 2, 20), pl = GenMeshPlane(2, 2, 3, 3);
    CHECK(cube.triangleCount == 12);
    CHECK(badWinding(cube, true) == 0);
    CHECK(badWinding(sph, true) == 0);
    CHECK(badWinding(cyl, true) == 0);
    CHECK(badWinding(cone, true) == 0);
    CHECK(badWinding(pl, true) == 0);

    // the game's shader sources resolve to precompiled shaders by hash; unknown ones do not
    const char* vs = "#version 330\nvoid main(){}";
    CHECK(foundry::render::findPortedShader(vs, vs) == nullptr);
    CHECK(foundry::render::findPortedShaderByName("gunsmith_lit") != nullptr);
    CHECK(foundry::render::findPortedShaderByName("builtin_default") != nullptr);

    // generated images
    Image im = GenImageColor(4, 4, Color{10, 20, 30, 255});
    ImageDrawRectangle(&im, 1, 1, 2, 2, Color{200, 0, 0, 255});
    auto* px = static_cast<unsigned char*>(im.data);
    CHECK(px[0] == 10 && px[(1 * 4 + 1) * 4] == 200 && px[(3 * 4 + 3) * 4 + 2] == 30);
    UnloadImage(im);
    CHECK(TextFormat("%d-%s", 7, "x")[2] == 'x');
    std::printf(failures ? "%d failure(s)\n" : "compat tests passed\n", failures);
    return failures;
}
