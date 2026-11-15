#include "core/Math.hpp"
#include "rhi/RHI.hpp"
#include <cstdio>

using namespace foundry;

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL: %s\n", #c); ++failures; } } while (0)

int main() {
    CHECK(dot({1, 0, 0}, {0, 1, 0}) == 0.0f);
    Vec3 c = cross({1, 0, 0}, {0, 1, 0});
    CHECK(c.z == 1.0f);
    CHECK(std::abs(length(normalize({3, 4, 0})) - 1.0f) < 1e-6f);
    std::printf(failures ? "%d failure(s)\n" : "all tests passed\n", failures);
    return failures;
}
