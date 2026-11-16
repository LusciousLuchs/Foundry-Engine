#include "core/Math.hpp"
#include "rhi/RHI.hpp"
#include "compat/raylib.hpp"
#include <cstdio>

using namespace foundry;

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL: %s\n", #c); ++failures; } } while (0)

int main() {
    CHECK(dot({1, 0, 0}, {0, 1, 0}) == 0.0f);
    Vec3 c = cross({1, 0, 0}, {0, 1, 0});
    CHECK(c.z == 1.0f);
    CHECK(std::abs(length(normalize({3, 4, 0})) - 1.0f) < 1e-6f);
    {   // picking: world -> screen -> ray must pass through the original point
        using namespace foundry::compat;
        Camera3D cam{{6, 4, 8}, {0, 1, 0}, {0, 1, 0}, 50.0f, CAMERA_PERSPECTIVE};
        Vector3 pts[] = {{0, 1, 0}, {2, 0.5f, -1}, {-3, 2, 1.5f}};
        for (Vector3 w : pts) {
            Vector2 s = GetWorldToScreenEx(w, cam, 1280, 720);
            Ray r = GetScreenToWorldRayEx(s, cam, 1280, 720);
            Vector3 toW = Vector3Subtract(w, r.position);
            float along = Vector3DotProduct(toW, r.direction);
            float off = Vector3Length(Vector3Subtract(toW, Vector3Scale(r.direction, along)));
            CHECK(along > 0 && off < 1e-2f);
        }
        Vector2 c = GetWorldToScreenEx(cam.target, cam, 1280, 720);
        CHECK(std::abs(c.x - 640) < 0.5f && std::abs(c.y - 360) < 0.5f);   // target projects to screen centre
        Ray down{{0.25f, 5, 0.25f}, {0, -1, 0}};
        RayCollision hit = GetRayCollisionTriangle(down, {0, 0, 0}, {0, 0, 1}, {1, 0, 0});
        CHECK(hit.hit && std::abs(hit.distance - 5.0f) < 1e-4f);
        CHECK(!GetRayCollisionTriangle({{2, 5, 2}, {0, -1, 0}}, {0, 0, 0}, {0, 0, 1}, {1, 0, 0}).hit);
        CHECK(GetColor(0xFF8000FFu).g == 0x80);
    }
    std::printf(failures ? "%d failure(s)\n" : "all tests passed\n", failures);
    return failures;
}
