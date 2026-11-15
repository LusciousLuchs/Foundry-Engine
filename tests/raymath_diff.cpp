// Differential test: foundry::compat math vs. the real raymath.h bundled with
// the game being ported. Build with -DFOUNDRY_RAYLIB_DIR=<.../third_party/raylib/src>.
#include "compat/raymath.hpp"
#include <cstdio>
#include <cstdlib>
#include <cmath>

#define RAYMATH_STATIC_INLINE
#include <raylib.h>
#include <raymath.h>

namespace fc = foundry::compat;

static int fails = 0, checks = 0;
static float frand() { return float(std::rand()) / RAND_MAX * 4.0f - 2.0f; }
static void near(const char* what, float a, float b) {
    ++checks;
    if (std::fabs(a - b) > 1e-4f * (1.0f + std::fabs(b))) { ++fails; std::printf("MISMATCH %s: %g vs %g\n", what, a, b); }
}
static void cmpM(const char* w, fc::Matrix a, ::Matrix b) {
    const float* pa = &a.m0; const float* pb = &b.m0;
    for (int i = 0; i < 16; ++i) near(w, pa[i], pb[i]);
}
static void cmpV(const char* w, fc::Vector3 a, ::Vector3 b) { near(w, a.x, b.x); near(w, a.y, b.y); near(w, a.z, b.z); }
static void cmpV2(const char* w, fc::Vector2 a, ::Vector2 b) { near(w, a.x, b.x); near(w, a.y, b.y); }

static_assert(sizeof(fc::Matrix) == sizeof(::Matrix) && sizeof(fc::Vector3) == sizeof(::Vector3));

int main() {
    std::srand(1234);
    for (int n = 0; n < 500; ++n) {
        fc::Vector3 a{frand(), frand(), frand()}, b{frand(), frand(), frand()};
        ::Vector3 ra{a.x, a.y, a.z}, rb{b.x, b.y, b.z};
        float s = frand(), ang = frand();
        cmpV("add", fc::Vector3Add(a, b), Vector3Add(ra, rb));
        cmpV("sub", fc::Vector3Subtract(a, b), Vector3Subtract(ra, rb));
        cmpV("scale", fc::Vector3Scale(a, s), Vector3Scale(ra, s));
        cmpV("neg", fc::Vector3Negate(a), Vector3Negate(ra));
        near("dot", fc::Vector3DotProduct(a, b), Vector3DotProduct(ra, rb));
        cmpV("cross", fc::Vector3CrossProduct(a, b), Vector3CrossProduct(ra, rb));
        near("len", fc::Vector3Length(a), Vector3Length(ra));
        near("lensqr", fc::Vector3LengthSqr(a), Vector3LengthSqr(ra));
        near("dist", fc::Vector3Distance(a, b), Vector3Distance(ra, rb));
        cmpV("norm", fc::Vector3Normalize(a), Vector3Normalize(ra));
        cmpV("lerp", fc::Vector3Lerp(a, b, s), Vector3Lerp(ra, rb, s));
        cmpV("rotaxis", fc::Vector3RotateByAxisAngle(a, b, ang), Vector3RotateByAxisAngle(ra, rb, ang));

        fc::Vector2 a2{a.x, a.y}, b2{b.x, b.y}; ::Vector2 ra2{a.x, a.y}, rb2{b.x, b.y};
        cmpV2("v2add", fc::Vector2Add(a2, b2), Vector2Add(ra2, rb2));
        cmpV2("v2sub", fc::Vector2Subtract(a2, b2), Vector2Subtract(ra2, rb2));
        cmpV2("v2scale", fc::Vector2Scale(a2, s), Vector2Scale(ra2, s));
        cmpV2("v2norm", fc::Vector2Normalize(a2), Vector2Normalize(ra2));
        cmpV2("v2lerp", fc::Vector2Lerp(a2, b2, s), Vector2Lerp(ra2, rb2, s));
        near("v2dist", fc::Vector2Distance(a2, b2), Vector2Distance(ra2, rb2));
        near("v2dot", fc::Vector2DotProduct(a2, b2), Vector2DotProduct(ra2, rb2));
        near("v2len", fc::Vector2Length(a2), Vector2Length(ra2));
        near("v2lensqr", fc::Vector2LengthSqr(a2), Vector2LengthSqr(ra2));

        fc::Matrix m1 = fc::MatrixMultiply(fc::MatrixRotate(b, ang), fc::MatrixTranslate(a.x, a.y, a.z));
        ::Matrix r1 = MatrixMultiply(MatrixRotate(rb, ang), MatrixTranslate(ra.x, ra.y, ra.z));
        cmpM("rot*trans", m1, r1);
        fc::Matrix m2 = fc::MatrixMultiply(fc::MatrixScale(s, 2, 3), fc::MatrixRotateX(ang));
        ::Matrix r2 = MatrixMultiply(MatrixScale(s, 2, 3), MatrixRotateX(ang));
        cmpM("scale*rx", m2, r2);
        cmpM("ry", fc::MatrixRotateY(ang), MatrixRotateY(ang));
        cmpM("rz", fc::MatrixRotateZ(ang), MatrixRotateZ(ang));
        cmpM("identity", fc::MatrixIdentity(), MatrixIdentity());
        cmpM("invert", fc::MatrixInvert(fc::MatrixMultiply(m1, m2)), MatrixInvert(MatrixMultiply(r1, r2)));
        cmpV("transform", fc::Vector3Transform(a, m1), Vector3Transform(ra, r1));
        cmpM("persp", fc::MatrixPerspective(1.0, 1.6, 0.1, 100.0), MatrixPerspective(1.0, 1.6, 0.1, 100.0));
        cmpM("lookat", fc::MatrixLookAt(a, b, {0, 1, 0}), MatrixLookAt(ra, rb, {0, 1, 0}));

        fc::Matrix proj = fc::MatrixPerspective(1.0, 1.6, 0.1, 100.0), view = fc::MatrixLookAt({0, 3, 8}, {0, 0, 0}, {0, 1, 0});
        ::Matrix rproj = MatrixPerspective(1.0, 1.6, 0.1, 100.0), rview = MatrixLookAt({0, 3, 8}, {0, 0, 0}, {0, 1, 0});
        fc::Vector3 sp{frand() * 0.4f, frand() * 0.4f, 0.5f};
        cmpV("unproject", fc::Vector3Unproject(sp, proj, view), Vector3Unproject({sp.x, sp.y, sp.z}, rproj, rview));
        fc::float16 f = fc::MatrixToFloatV(m1); ::float16 rf = MatrixToFloatV(r1);
        for (int i = 0; i < 16; ++i) near("tofloat", f.v[i], rf.v[i]);
    }
    std::printf("%d checks, %d mismatches\n", checks, fails);
    return fails ? 1 : 0;
}
