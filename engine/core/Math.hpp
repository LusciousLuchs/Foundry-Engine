#pragma once
#include <cmath>

namespace foundry {

struct Vec3 {
    float x = 0, y = 0, z = 0;
    constexpr Vec3 operator+(Vec3 o) const { return {x + o.x, y + o.y, z + o.z}; }
    constexpr Vec3 operator-(Vec3 o) const { return {x - o.x, y - o.y, z - o.z}; }
    constexpr Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
};

constexpr float dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
constexpr Vec3 cross(Vec3 a, Vec3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline float length(Vec3 v) { return std::sqrt(dot(v, v)); }
inline Vec3 normalize(Vec3 v) {
    float l = length(v);
    return l > 0 ? v * (1.0f / l) : Vec3{};
}

} // namespace foundry
