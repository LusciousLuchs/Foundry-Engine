#pragma once
// raylib-compatible math for porting. Same type layouts and function names as
// raylib/raymath.h (5.5), in namespace foundry::compat. Port code with:
//     using namespace foundry::compat;
// Matrices follow raylib's convention (row-vector, v' = v * M, translation in
// m12..m14). Verified against the real raymath.h by tests/raymath_diff.cpp.
#include <cmath>
#include <cstring>

namespace foundry::compat {

struct Vector2 { float x, y; };
struct Vector3 { float x, y, z; };
struct Vector4 { float x, y, z, w; };
using Quaternion = Vector4;
struct Matrix {
    float m0, m4, m8, m12;
    float m1, m5, m9, m13;
    float m2, m6, m10, m14;
    float m3, m7, m11, m15;
};
struct Color { unsigned char r, g, b, a; };
struct float16 { float v[16]; };

namespace foundry_internal {
// Matrix <-> array in declaration order is column-major (m0,m4,m8,m12 first
// field row), so index a[c*4+r] == m_(r + c*4)... we use the raylib naming
// directly: element m<i> lives at index (i%4)*4 + i/4 of the struct.
inline void toArr(const Matrix& m, float a[16]) {
    const float t[16] = {m.m0, m.m1, m.m2, m.m3, m.m4, m.m5, m.m6, m.m7,
                         m.m8, m.m9, m.m10, m.m11, m.m12, m.m13, m.m14, m.m15};
    std::memcpy(a, t, sizeof t);
}
inline Matrix fromArr(const float a[16]) {
    return {a[0], a[4], a[8],  a[12], a[1], a[5], a[9],  a[13],
            a[2], a[6], a[10], a[14], a[3], a[7], a[11], a[15]};
}
} // namespace foundry_internal

// ---- Vector2 ----
inline Vector2 Vector2Add(Vector2 a, Vector2 b) { return {a.x + b.x, a.y + b.y}; }
inline Vector2 Vector2Subtract(Vector2 a, Vector2 b) { return {a.x - b.x, a.y - b.y}; }
inline Vector2 Vector2Scale(Vector2 v, float s) { return {v.x * s, v.y * s}; }
inline float Vector2DotProduct(Vector2 a, Vector2 b) { return a.x * b.x + a.y * b.y; }
inline float Vector2LengthSqr(Vector2 v) { return v.x * v.x + v.y * v.y; }
inline float Vector2Length(Vector2 v) { return std::sqrt(Vector2LengthSqr(v)); }
inline float Vector2Distance(Vector2 a, Vector2 b) {
    return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y));
}
inline Vector2 Vector2Normalize(Vector2 v) {
    Vector2 r{0, 0};
    float l = Vector2Length(v);
    if (l > 0) { float i = 1.0f / l; r = {v.x * i, v.y * i}; }
    return r;
}
inline Vector2 Vector2Lerp(Vector2 a, Vector2 b, float t) {
    return {a.x + t * (b.x - a.x), a.y + t * (b.y - a.y)};
}

// ---- Vector3 ----
inline Vector3 Vector3Add(Vector3 a, Vector3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vector3 Vector3Subtract(Vector3 a, Vector3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vector3 Vector3Scale(Vector3 v, float s) { return {v.x * s, v.y * s, v.z * s}; }
inline Vector3 Vector3Negate(Vector3 v) { return {-v.x, -v.y, -v.z}; }
inline float Vector3DotProduct(Vector3 a, Vector3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vector3 Vector3CrossProduct(Vector3 a, Vector3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline float Vector3LengthSqr(Vector3 v) { return v.x * v.x + v.y * v.y + v.z * v.z; }
inline float Vector3Length(Vector3 v) { return std::sqrt(Vector3LengthSqr(v)); }
inline float Vector3Distance(Vector3 a, Vector3 b) { return Vector3Length(Vector3Subtract(a, b)); }
inline Vector3 Vector3Normalize(Vector3 v) {
    Vector3 r = v;
    float l = Vector3Length(v);
    if (l != 0.0f) { float i = 1.0f / l; r = {v.x * i, v.y * i, v.z * i}; }
    return r;
}
inline Vector3 Vector3Lerp(Vector3 a, Vector3 b, float t) {
    return {a.x + t * (b.x - a.x), a.y + t * (b.y - a.y), a.z + t * (b.z - a.z)};
}
inline Vector3 Vector3Transform(Vector3 v, Matrix m) {
    return {m.m0 * v.x + m.m4 * v.y + m.m8 * v.z + m.m12,
            m.m1 * v.x + m.m5 * v.y + m.m9 * v.z + m.m13,
            m.m2 * v.x + m.m6 * v.y + m.m10 * v.z + m.m14};
}
inline Vector3 Vector3RotateByAxisAngle(Vector3 v, Vector3 axis, float angle) {
    float len = Vector3Length(axis);
    if (len == 0.0f) len = 1.0f;
    axis = Vector3Scale(axis, 1.0f / len);
    angle /= 2.0f;
    float a = std::sin(angle);
    Vector3 w{axis.x * a, axis.y * a, axis.z * a};
    a = std::cos(angle);
    Vector3 wv = Vector3CrossProduct(w, v);
    Vector3 wwv = Vector3CrossProduct(w, wv);
    wv = Vector3Scale(wv, a * 2);
    wwv = Vector3Scale(wwv, 2);
    return Vector3Add(Vector3Add(v, wv), wwv);
}

// ---- Matrix ----
inline Matrix MatrixIdentity() { return {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}; }
inline Matrix MatrixMultiply(Matrix left, Matrix right) {
    return {
        left.m0 * right.m0 + left.m1 * right.m4 + left.m2 * right.m8 + left.m3 * right.m12,
        left.m4 * right.m0 + left.m5 * right.m4 + left.m6 * right.m8 + left.m7 * right.m12,
        left.m8 * right.m0 + left.m9 * right.m4 + left.m10 * right.m8 + left.m11 * right.m12,
        left.m12 * right.m0 + left.m13 * right.m4 + left.m14 * right.m8 + left.m15 * right.m12,
        left.m0 * right.m1 + left.m1 * right.m5 + left.m2 * right.m9 + left.m3 * right.m13,
        left.m4 * right.m1 + left.m5 * right.m5 + left.m6 * right.m9 + left.m7 * right.m13,
        left.m8 * right.m1 + left.m9 * right.m5 + left.m10 * right.m9 + left.m11 * right.m13,
        left.m12 * right.m1 + left.m13 * right.m5 + left.m14 * right.m9 + left.m15 * right.m13,
        left.m0 * right.m2 + left.m1 * right.m6 + left.m2 * right.m10 + left.m3 * right.m14,
        left.m4 * right.m2 + left.m5 * right.m6 + left.m6 * right.m10 + left.m7 * right.m14,
        left.m8 * right.m2 + left.m9 * right.m6 + left.m10 * right.m10 + left.m11 * right.m14,
        left.m12 * right.m2 + left.m13 * right.m6 + left.m14 * right.m10 + left.m15 * right.m14,
        left.m0 * right.m3 + left.m1 * right.m7 + left.m2 * right.m11 + left.m3 * right.m15,
        left.m4 * right.m3 + left.m5 * right.m7 + left.m6 * right.m11 + left.m7 * right.m15,
        left.m8 * right.m3 + left.m9 * right.m7 + left.m10 * right.m11 + left.m11 * right.m15,
        left.m12 * right.m3 + left.m13 * right.m7 + left.m14 * right.m11 + left.m15 * right.m15};
}
inline Matrix MatrixTranslate(float x, float y, float z) {
    return {1, 0, 0, x, 0, 1, 0, y, 0, 0, 1, z, 0, 0, 0, 1};
}
inline Matrix MatrixScale(float x, float y, float z) {
    return {x, 0, 0, 0, 0, y, 0, 0, 0, 0, z, 0, 0, 0, 0, 1};
}
inline Matrix MatrixRotate(Vector3 axis, float angle) {
    Matrix r = MatrixIdentity();
    float x = axis.x, y = axis.y, z = axis.z;
    float l2 = x * x + y * y + z * z;
    if (l2 != 1.0f && l2 != 0.0f) {
        float i = 1.0f / std::sqrt(l2);
        x *= i; y *= i; z *= i;
    }
    float s = std::sin(angle), c = std::cos(angle), t = 1.0f - c;
    r.m0 = x * x * t + c;      r.m1 = y * x * t + z * s;  r.m2 = z * x * t - y * s;
    r.m4 = x * y * t - z * s;  r.m5 = y * y * t + c;      r.m6 = z * y * t + x * s;
    r.m8 = x * z * t + y * s;  r.m9 = y * z * t - x * s;  r.m10 = z * z * t + c;
    return r;
}
inline Matrix MatrixRotateX(float a) {
    Matrix r = MatrixIdentity(); float c = std::cos(a), s = std::sin(a);
    r.m5 = c; r.m6 = s; r.m9 = -s; r.m10 = c; return r;
}
inline Matrix MatrixRotateY(float a) {
    Matrix r = MatrixIdentity(); float c = std::cos(a), s = std::sin(a);
    r.m0 = c; r.m2 = -s; r.m8 = s; r.m10 = c; return r;
}
inline Matrix MatrixRotateZ(float a) {
    Matrix r = MatrixIdentity(); float c = std::cos(a), s = std::sin(a);
    r.m0 = c; r.m1 = s; r.m4 = -s; r.m5 = c; return r;
}
inline Matrix MatrixInvert(Matrix mat) {
    // General 4x4 inverse via cofactors (same expansion raylib uses).
    float a00 = mat.m0, a01 = mat.m1, a02 = mat.m2, a03 = mat.m3;
    float a10 = mat.m4, a11 = mat.m5, a12 = mat.m6, a13 = mat.m7;
    float a20 = mat.m8, a21 = mat.m9, a22 = mat.m10, a23 = mat.m11;
    float a30 = mat.m12, a31 = mat.m13, a32 = mat.m14, a33 = mat.m15;
    float b00 = a00 * a11 - a01 * a10, b01 = a00 * a12 - a02 * a10;
    float b02 = a00 * a13 - a03 * a10, b03 = a01 * a12 - a02 * a11;
    float b04 = a01 * a13 - a03 * a11, b05 = a02 * a13 - a03 * a12;
    float b06 = a20 * a31 - a21 * a30, b07 = a20 * a32 - a22 * a30;
    float b08 = a20 * a33 - a23 * a30, b09 = a21 * a32 - a22 * a31;
    float b10 = a21 * a33 - a23 * a31, b11 = a22 * a33 - a23 * a32;
    float det = b00 * b11 - b01 * b10 + b02 * b09 + b03 * b08 - b04 * b07 + b05 * b06;
    float id = 1.0f / det;
    Matrix r;
    r.m0 = (a11 * b11 - a12 * b10 + a13 * b09) * id;
    r.m1 = (-a01 * b11 + a02 * b10 - a03 * b09) * id;
    r.m2 = (a31 * b05 - a32 * b04 + a33 * b03) * id;
    r.m3 = (-a21 * b05 + a22 * b04 - a23 * b03) * id;
    r.m4 = (-a10 * b11 + a12 * b08 - a13 * b07) * id;
    r.m5 = (a00 * b11 - a02 * b08 + a03 * b07) * id;
    r.m6 = (-a30 * b05 + a32 * b02 - a33 * b01) * id;
    r.m7 = (a20 * b05 - a22 * b02 + a23 * b01) * id;
    r.m8 = (a10 * b10 - a11 * b08 + a13 * b06) * id;
    r.m9 = (-a00 * b10 + a01 * b08 - a03 * b06) * id;
    r.m10 = (a30 * b04 - a31 * b02 + a33 * b00) * id;
    r.m11 = (-a20 * b04 + a21 * b02 - a23 * b00) * id;
    r.m12 = (-a10 * b09 + a11 * b07 - a12 * b06) * id;
    r.m13 = (a00 * b09 - a01 * b07 + a02 * b06) * id;
    r.m14 = (-a30 * b03 + a31 * b01 - a32 * b00) * id;
    r.m15 = (a20 * b03 - a21 * b01 + a22 * b00) * id;
    return r;
}
inline Matrix MatrixPerspective(double fovY, double aspect, double nearPlane, double farPlane) {
    double top = nearPlane * std::tan(fovY * 0.5), bottom = -top;
    double right = top * aspect, left = -right;
    float rl = float(right - left), tb = float(top - bottom), fn = float(farPlane - nearPlane);
    Matrix r{};
    r.m0 = float(nearPlane * 2.0f) / rl;
    r.m5 = float(nearPlane * 2.0f) / tb;
    r.m8 = float(right + left) / rl;
    r.m9 = float(top + bottom) / tb;
    r.m10 = -float(farPlane + nearPlane) / fn;
    r.m11 = -1.0f;
    r.m14 = -float(farPlane * nearPlane * 2.0f) / fn;
    return r;
}
inline Matrix MatrixFrustum(double left, double right, double bottom, double top, double nearPlane, double farPlane) {
    float rl = float(right - left), tb = float(top - bottom), fn = float(farPlane - nearPlane);
    Matrix r{};
    r.m0 = float(nearPlane * 2.0f) / rl;
    r.m5 = float(nearPlane * 2.0f) / tb;
    r.m8 = float(right + left) / rl;
    r.m9 = float(top + bottom) / tb;
    r.m10 = -float(farPlane + nearPlane) / fn;
    r.m11 = -1.0f;
    r.m14 = -float(farPlane * nearPlane * 2.0f) / fn;
    return r;
}
inline Matrix MatrixOrtho(double left, double right, double bottom, double top, double nearPlane, double farPlane) {
    float rl = float(right - left), tb = float(top - bottom), fn = float(farPlane - nearPlane);
    Matrix r{};
    r.m0 = 2.0f / rl; r.m5 = 2.0f / tb; r.m10 = -2.0f / fn;
    r.m12 = -float(left + right) / rl; r.m13 = -float(top + bottom) / tb; r.m14 = -float(farPlane + nearPlane) / fn;
    r.m15 = 1.0f;
    return r;
}
inline Matrix MatrixLookAt(Vector3 eye, Vector3 target, Vector3 up) {
    Vector3 vz = Vector3Normalize(Vector3Subtract(eye, target));
    Vector3 vx = Vector3Normalize(Vector3CrossProduct(up, vz));
    Vector3 vy = Vector3CrossProduct(vz, vx);
    Matrix r{};
    r.m0 = vx.x; r.m1 = vy.x; r.m2 = vz.x;
    r.m4 = vx.y; r.m5 = vy.y; r.m6 = vz.y;
    r.m8 = vx.z; r.m9 = vy.z; r.m10 = vz.z;
    r.m12 = -Vector3DotProduct(vx, eye);
    r.m13 = -Vector3DotProduct(vy, eye);
    r.m14 = -Vector3DotProduct(vz, eye);
    r.m15 = 1.0f;
    return r;
}
inline float16 MatrixToFloatV(Matrix m) {
    float16 r;
    foundry_internal::toArr(m, r.v);
    return r;
}

// ---- Quaternion / Unproject ----
inline Vector4 QuaternionTransform(Vector4 q, Matrix m) {
    return {m.m0 * q.x + m.m4 * q.y + m.m8 * q.z + m.m12 * q.w,
            m.m1 * q.x + m.m5 * q.y + m.m9 * q.z + m.m13 * q.w,
            m.m2 * q.x + m.m6 * q.y + m.m10 * q.z + m.m14 * q.w,
            m.m3 * q.x + m.m7 * q.y + m.m11 * q.z + m.m15 * q.w};
}
inline Vector3 Vector3Unproject(Vector3 source, Matrix projection, Matrix view) {
    Matrix inv = MatrixInvert(MatrixMultiply(view, projection));
    Vector4 q = QuaternionTransform({source.x, source.y, source.z, 1.0f}, inv);
    return {q.x / q.w, q.y / q.w, q.z / q.w};
}

} // namespace foundry::compat
