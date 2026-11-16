// Windows-only checks that do not need a window or GPU (run under Wine in CI).
#include "compat/raylib.hpp"
#include "render/Renderer.hpp"
#include <cstdio>
#include <cstring>
#include <cmath>

#include "compat_test_data.inc"
#undef PlaySound   // (windows.h, pulled in by the renderer header, defines a PlaySound macro)
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
    {   // ray vs mesh: nearest hit, in world space after the transform
        Mesh c = GenMeshCube(2, 2, 2);
        RayCollision hit = GetRayCollisionMesh({{0, 0, 10}, {0, 0, -1}}, c, MatrixTranslate(0, 0, 0));
        CHECK(hit.hit && std::fabs(hit.distance - 9.0f) < 1e-4f && std::fabs(hit.normal.z - 1.0f) < 1e-4f);
        hit = GetRayCollisionMesh({{0, 0, 10}, {0, 0, -1}}, c, MatrixTranslate(0, 0, -5));
        CHECK(hit.hit && std::fabs(hit.distance - 14.0f) < 1e-4f);
        CHECK(!GetRayCollisionMesh({{5, 5, 10}, {0, 0, -1}}, c, MatrixIdentity()).hit);
        UnloadMesh(c);
    }
    CHECK(std::strcmp(GetFileExtension("a/b/gun.OBJ"), ".OBJ") == 0 && GetFileExtension("noext") == nullptr);
    CHECK(IsFileExtension("x.PNG", ".png") && IsFileExtension("x.jpg", ".png;.jpg") && !IsFileExtension("x.jpgx", ".jpg"));
    CHECK(std::strcmp(GetFileName("C:\\dir\\sub\\f.txt"), "f.txt") == 0 && std::strcmp(GetFileNameWithoutExt("/a/b/f.tar.gz"), "f.tar") == 0);
    {   // PNG decode (stb_image), resize, checker, DEFLATE
        Image png = LoadImageFromMemory(".png", kPng, int(sizeof kPng));
        CHECK(png.data && png.width == 3 && png.height == 2);
        auto* p = static_cast<unsigned char*>(png.data);
        CHECK(p[0] == 255 && p[1] == 0 && p[2] == 0 && p[3] == 255);                 // (0,0) red
        CHECK(p[4 + 1] == 255);                                                       // (1,0) green
        CHECK(p[(1 * 3 + 0) * 4 + 3] == 128 && p[(1 * 3 + 0) * 4 + 1] == 20);          // (0,1) 10,20,30,128
        ImageResize(&png, 6, 4);
        CHECK(png.width == 6 && png.height == 4);
        auto* q = static_cast<unsigned char*>(png.data);
        CHECK(q[0] == 255 && q[1] == 0);                                              // corner stays pure red
        CHECK(q[(0 * 6 + 5) * 4 + 3] == 255);                                        // top-right stays opaque
        UnloadImage(png);
        CHECK(LoadImageFromMemory(".png", reinterpret_cast<const unsigned char*>(kPlain), 8).data == nullptr);                 // garbage is rejected, not decoded
        Image chk = GenImageChecked(16, 16, 4, 4, Color{255, 0, 255, 255}, Color{0, 0, 0, 255});
        auto* c = static_cast<unsigned char*>(chk.data);
        CHECK(c[0] == 255 && c[(0 * 16 + 4) * 4] == 0 && c[(4 * 16 + 4) * 4] == 255); // 4px checks alternate
        UnloadImage(chk);
        int n = 0;
        unsigned char* d = DecompressData(kDeflate, int(sizeof kDeflate), &n);
        CHECK(d && n == int(sizeof kPlain) - 1 && std::memcmp(d, kPlain, size_t(n)) == 0);
        std::free(d);
        int bad = 0;
        CHECK(DecompressData(kPng, 5, &bad) == nullptr || bad >= 0);                   // corrupt input must not crash
    }
#ifdef FOUNDRY_HAS_AUDIO
    {   // raylib's audio module: decoding works without a device; a machine with no sound card must not crash us
        Wave w = LoadWaveFromMemory(".wav", kWav, int(sizeof kWav));
        CHECK(IsWaveValid(w) && w.frameCount == 100 && w.sampleRate == 8000 && w.sampleSize == 16 && w.channels == 1);
        InitAudioDevice();
        bool ready = IsAudioDeviceReady();
        std::printf("audio device ready: %s\n", ready ? "yes" : "no (expected on a machine without a sound device)");
        if (ready) {
            Sound s = LoadSoundFromWave(w);
            CHECK(IsSoundValid(s) && s.frameCount == 100);
            SetSoundVolume(s, 0.5f); SetSoundPitch(s, 1.2f);
            PlaySound(s);
            StopSound(s);
            UnloadSound(s);
        }
        UnloadWave(w);
        CloseAudioDevice();
    }
#endif
    std::printf(failures ? "%d failure(s)\n" : "compat tests passed\n", failures);
    return failures;
}
