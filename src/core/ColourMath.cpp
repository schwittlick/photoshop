#include "core/ColourMath.h"
#include <cmath>

namespace re::colour {

const Chromaticity kD65{0.31271, 0.32902};
const Chromaticity kD50{0.34567, 0.35850};
const Chromaticity kRec2020[3] = {{0.708, 0.292}, {0.170, 0.797}, {0.131, 0.046}};
const Chromaticity kSRGB[3] = {{0.64, 0.33}, {0.30, 0.60}, {0.15, 0.06}};
const Chromaticity kAdobeRGB[3] = {{0.64, 0.33}, {0.21, 0.71}, {0.15, 0.06}};
const Chromaticity kDisplayP3[3] = {{0.680, 0.320}, {0.265, 0.690}, {0.150, 0.060}};

Vec3 xyToXYZ(Chromaticity c, double Y) {
    if (c.y <= 0) return {0, 0, 0};
    return {c.x * Y / c.y, Y, (1.0 - c.x - c.y) * Y / c.y};
}

Vec2 xyToUv(Chromaticity c) {
    double d = -2.0 * c.x + 12.0 * c.y + 3.0;
    return {4.0 * c.x / d, 6.0 * c.y / d};
}

Chromaticity uvToXy(Vec2 uv) {
    double d = 2.0 * uv.x - 8.0 * uv.y + 4.0;
    return {3.0 * uv.x / d, 2.0 * uv.y / d};
}

Mat3 rgbToXyz(const Chromaticity prim[3], Chromaticity white) {
    // Columns are the primaries' XYZ (with Y unknown); solve scale so the white maps to Y=1.
    Mat3 P = Mat3::fromColumns(xyToXYZ(prim[0]), xyToXYZ(prim[1]), xyToXYZ(prim[2]));
    Vec3 W = xyToXYZ(white);
    Vec3 S = P.inverse() * W;
    return P * Mat3::diag(S);
}

Mat3 bradford(const Vec3& srcWhite, const Vec3& dstWhite) {
    static const Mat3 B = Mat3::fromRows({0.8951, 0.2664, -0.1614},
                                         {-0.7502, 1.7135, 0.0367},
                                         {0.0389, -0.0685, 1.0296});
    static const Mat3 Binv = B.inverse();
    Vec3 s = B * srcWhite, d = B * dstWhite;
    return Binv * Mat3::diag({d.x / s.x, d.y / s.y, d.z / s.z}) * B;
}

Mat3 workingToXyzD50() {
    static const Mat3 M = bradford(xyToXYZ(kD65), xyToXYZ(kD50)) * rgbToXyz(kRec2020, kD65);
    return M;
}
Mat3 xyzD50ToWorking() { static const Mat3 M = workingToXyzD50().inverse(); return M; }

Mat3 workingToLinear(const Chromaticity prim[3]) {
    // Both spaces D65 with the same white, so a plain matrix product is exact and white-preserving.
    return rgbToXyz(prim, kD65).inverse() * rgbToXyz(kRec2020, kD65);
}
Mat3 workingToLinearSRGB() { static const Mat3 M = workingToLinear(kSRGB); return M; }
Mat3 linearSRGBToWorking() { static const Mat3 M = workingToLinearSRGB().inverse(); return M; }

Chromaticity planckianXy(double T) {
    T = clampd(T, 1667.0, 25000.0);
    double t = 1.0 / T, t2 = t * t, t3 = t2 * t;
    double x = (T <= 4000.0)
                   ? -0.2661239e9 * t3 - 0.2343589e6 * t2 + 0.8776956e3 * t + 0.179910
                   : -3.0258469e9 * t3 + 2.1070379e6 * t2 + 0.2226347e3 * t + 0.240390;
    double x2 = x * x, x3 = x2 * x, y;
    if (T <= 2222.0)      y = -1.1063814 * x3 - 1.34811020 * x2 + 2.18555832 * x - 0.20219683;
    else if (T <= 4000.0) y = -0.9549476 * x3 - 1.37418593 * x2 + 2.09137015 * x - 0.16748867;
    else                  y =  3.0817580 * x3 - 5.87338670 * x2 + 3.75112997 * x - 0.37001483;
    return {x, y};
}

namespace {

// Locus point plus a unit normal pointing to the green side (increasing v).
struct LocusPoint { Vec2 uv, normal; };

LocusPoint locusAt(double T) {
    Vec2 uv = xyToUv(planckianXy(T));
    double dT = std::max(1.0, T * 0.002);
    Vec2 a = xyToUv(planckianXy(clampd(T - dT, 1667.0, 25000.0)));
    Vec2 b = xyToUv(planckianXy(clampd(T + dT, 1667.0, 25000.0)));
    Vec2 tangent = (b - a).normalized();
    Vec2 n(-tangent.y, tangent.x);
    if (n.y < 0) n = n * -1.0;
    return {uv, n};
}

Vec2 uvFromTempTint(double T, double tint) {
    LocusPoint lp = locusAt(clampd(T, kTempMin, kTempMax));
    return lp.uv + lp.normal * (tint / kTintScale);
}

}  // namespace

Mat3 CameraColour::camToWorking() const {
    Vec3 whiteD65 = xyToXYZ(kD65);
    // Row-normalise so that a white-balanced neutral (1,1,1) lands exactly on D65 white.
    Vec3 cw = xyzToCam * whiteD65;
    Mat3 camToXyz = xyzToCam.inverse() * Mat3::diag(cw);
    Mat3 adapt = bradford(whiteD65, xyToXYZ(kD50));
    return xyzD50ToWorking() * adapt * camToXyz;
}

Vec3 CameraColour::multipliersFromTempTint(double T, double tint) const {
    Vec3 xyz = xyToXYZ(uvToXy(uvFromTempTint(T, tint)));
    Vec3 c = xyzToCam * xyz;
    Vec3 mul(1.0 / std::max(c.x, 1e-6), 1.0 / std::max(c.y, 1e-6), 1.0 / std::max(c.z, 1e-6));
    return mul / mul.y;
}

std::pair<double, double> CameraColour::tempTintFromMultipliers(const Vec3& mul) const {
    Vec3 c(1.0 / std::max(double(mul.x), 1e-6), 1.0 / std::max(double(mul.y), 1e-6), 1.0 / std::max(double(mul.z), 1e-6));
    Vec3 xyz = xyzToCam.inverse() * c;
    double sum = xyz.x + xyz.y + xyz.z;
    if (sum <= 0) return {5000.0, 0.0};
    Vec2 uv = xyToUv({xyz.x / sum, xyz.y / sum});

    // Golden-section search over mired for the closest locus point.
    auto dist = [&](double mired) { return (locusAt(1e6 / mired).uv - uv).length(); };
    double lo = 1e6 / kTempMax, hi = 1e6 / kTempMin;
    const double gr = (std::sqrt(5.0) - 1.0) / 2.0;
    double a = hi - gr * (hi - lo), b = lo + gr * (hi - lo);
    double fa = dist(a), fb = dist(b);
    for (int i = 0; i < 100; ++i) {
        if (fa < fb) { hi = b; b = a; fb = fa; a = hi - gr * (hi - lo); fa = dist(a); }
        else         { lo = a; a = b; fa = fb; b = lo + gr * (hi - lo); fb = dist(b); }
    }
    double mired = 0.5 * (lo + hi);
    double T = 1e6 / mired;
    LocusPoint lp = locusAt(T);
    double tint = (uv - lp.uv).dot(lp.normal) * kTintScale;
    return {T, tint};
}

CameraColour CameraColour::fromLibRaw(const float camXyz[4][3], const float rgbCam[3][4], int colours) {
    CameraColour cc;
    Mat3 M = Mat3::zero();
    double sum = 0;
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c) { M.m[r][c] = camXyz[r][c]; sum += std::abs(camXyz[r][c]); }
    if (colours >= 3 && sum > 1e-6 && std::abs(M.det()) > 1e-12) {
        cc.xyzToCam = M;
        cc.valid = true;
        return cc;
    }
    // Fallback: LibRaw's rgb_cam maps white-balanced camera -> linear sRGB (D65).
    Mat3 R = Mat3::zero();
    sum = 0;
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c) { R.m[r][c] = rgbCam[r][c]; sum += std::abs(rgbCam[r][c]); }
    if (sum > 1e-6 && std::abs(R.det()) > 1e-12) {
        cc.xyzToCam = R.inverse() * rgbToXyz(kSRGB, kD65).inverse();
        cc.valid = true;
        return cc;
    }
    return fallbackSRGB();
}

CameraColour CameraColour::fallbackSRGB() {
    CameraColour cc;
    cc.xyzToCam = rgbToXyz(kSRGB, kD65).inverse();
    cc.valid = false;
    return cc;
}

}  // namespace re::colour
