#pragma once
// Small double-precision linear algebra used by the CPU side of the pipeline.
// The GPU side receives float copies of these (column-major for GLSL).
#include <array>
#include <cmath>
#include <algorithm>

namespace re {

struct Vec2 {
    double x = 0, y = 0;
    Vec2() = default;
    Vec2(double x_, double y_) : x(x_), y(y_) {}
    Vec2 operator+(const Vec2& o) const { return {x + o.x, y + o.y}; }
    Vec2 operator-(const Vec2& o) const { return {x - o.x, y - o.y}; }
    Vec2 operator*(double s) const { return {x * s, y * s}; }
    Vec2 operator/(double s) const { return {x / s, y / s}; }
    Vec2 operator*(const Vec2& o) const { return {x * o.x, y * o.y}; }
    Vec2 operator/(const Vec2& o) const { return {x / o.x, y / o.y}; }
    Vec2& operator+=(const Vec2& o) { x += o.x; y += o.y; return *this; }
    double length() const { return std::sqrt(x * x + y * y); }
    double dot(const Vec2& o) const { return x * o.x + y * o.y; }
    Vec2 normalized() const { double l = length(); return l > 0 ? *this / l : *this; }
};

struct Vec3 {
    double x = 0, y = 0, z = 0;
    Vec3() = default;
    Vec3(double x_, double y_, double z_) : x(x_), y(y_), z(z_) {}
    double operator[](int i) const { return i == 0 ? x : (i == 1 ? y : z); }
    double& operator[](int i) { return i == 0 ? x : (i == 1 ? y : z); }
    Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vec3 operator*(double s) const { return {x * s, y * s, z * s}; }
    Vec3 operator/(double s) const { return {x / s, y / s, z / s}; }
    Vec3 operator*(const Vec3& o) const { return {x * o.x, y * o.y, z * o.z}; }
    Vec3 operator/(const Vec3& o) const { return {x / o.x, y / o.y, z / o.z}; }
    double maxComponent() const { return std::max(x, std::max(y, z)); }
    double minComponent() const { return std::min(x, std::min(y, z)); }
};

struct Mat3 {
    double m[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};  // row-major: m[row][col]

    static Mat3 identity() { return Mat3(); }
    static Mat3 zero() { Mat3 r; for (auto& row : r.m) for (double& v : row) v = 0; return r; }
    static Mat3 diag(const Vec3& d) { Mat3 r = zero(); r.m[0][0] = d.x; r.m[1][1] = d.y; r.m[2][2] = d.z; return r; }
    static Mat3 fromRows(const Vec3& a, const Vec3& b, const Vec3& c) {
        Mat3 r;
        for (int i = 0; i < 3; ++i) { r.m[0][i] = a[i]; r.m[1][i] = b[i]; r.m[2][i] = c[i]; }
        return r;
    }
    static Mat3 fromColumns(const Vec3& a, const Vec3& b, const Vec3& c) { return fromRows(a, b, c).transposed(); }

    Vec3 row(int i) const { return {m[i][0], m[i][1], m[i][2]}; }
    Vec3 col(int j) const { return {m[0][j], m[1][j], m[2][j]}; }

    Mat3 operator*(const Mat3& o) const {
        Mat3 r = zero();
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j)
                for (int k = 0; k < 3; ++k) r.m[i][j] += m[i][k] * o.m[k][j];
        return r;
    }
    Vec3 operator*(const Vec3& v) const {
        return {m[0][0] * v.x + m[0][1] * v.y + m[0][2] * v.z,
                m[1][0] * v.x + m[1][1] * v.y + m[1][2] * v.z,
                m[2][0] * v.x + m[2][1] * v.y + m[2][2] * v.z};
    }
    Mat3 operator*(double s) const { Mat3 r = *this; for (auto& row : r.m) for (double& v : row) v *= s; return r; }
    Mat3 transposed() const {
        Mat3 r;
        for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) r.m[i][j] = m[j][i];
        return r;
    }
    double det() const {
        return m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1])
             - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0])
             + m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
    }
    Mat3 inverse() const {
        double d = det();
        Mat3 r = zero();
        if (std::abs(d) < 1e-300) return r;
        double id = 1.0 / d;
        r.m[0][0] =  (m[1][1] * m[2][2] - m[1][2] * m[2][1]) * id;
        r.m[0][1] = -(m[0][1] * m[2][2] - m[0][2] * m[2][1]) * id;
        r.m[0][2] =  (m[0][1] * m[1][2] - m[0][2] * m[1][1]) * id;
        r.m[1][0] = -(m[1][0] * m[2][2] - m[1][2] * m[2][0]) * id;
        r.m[1][1] =  (m[0][0] * m[2][2] - m[0][2] * m[2][0]) * id;
        r.m[1][2] = -(m[0][0] * m[1][2] - m[0][2] * m[1][0]) * id;
        r.m[2][0] =  (m[1][0] * m[2][1] - m[1][1] * m[2][0]) * id;
        r.m[2][1] = -(m[0][0] * m[2][1] - m[0][1] * m[2][0]) * id;
        r.m[2][2] =  (m[0][0] * m[1][1] - m[0][1] * m[1][0]) * id;
        return r;
    }
    // Column-major float copy, the layout GLSL's mat3 expects.
    std::array<float, 9> toGL() const {
        std::array<float, 9> a{};
        for (int c = 0; c < 3; ++c) for (int r = 0; r < 3; ++r) a[c * 3 + r] = float(m[r][c]);
        return a;
    }
    // Homography application (projective divide).
    Vec2 apply(const Vec2& p) const {
        Vec3 h = (*this) * Vec3(p.x, p.y, 1.0);
        if (std::abs(h.z) < 1e-12) return {0, 0};
        return {h.x / h.z, h.y / h.z};
    }
};

inline double clampd(double v, double lo, double hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline double smoothstepd(double e0, double e1, double x) {
    double t = clampd((x - e0) / (e1 - e0), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}
constexpr double kPi = 3.14159265358979323846;

}  // namespace re
