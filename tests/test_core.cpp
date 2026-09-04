// Unit tests for the CPU side: colour maths, curves, history, geometry, lensfun wrapper.
#include "core/ColourMath.h"
#include "core/CurveModel.h"
#include "core/Geometry.h"
#include "core/History.h"
#include "core/LensModel.h"
#include "core/OutputProfiles.h"
#include <cmath>
#include <cstdio>

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); ++failures; } } while (0)
#define CHECK_NEAR(a, b, eps) do { double _a = (a), _b = (b); if (std::abs(_a - _b) > (eps)) { std::printf("  FAIL %s:%d: %s = %g, expected %g (+-%g)\n", __FILE__, __LINE__, #a, _a, _b, double(eps)); ++failures; } } while (0)

using namespace re;

static void testColour() {
    std::printf("colour\n");
    // Adobe matrix for a Sony ILCE-7 (LibRaw adobe_coeff), XYZ -> camera.
    float camxyz[4][3] = {{0.5271f, -0.0712f, -0.0347f}, {-0.6153f, 1.3653f, 0.2763f}, {-0.1601f, 0.2366f, 0.7242f}, {0, 0, 0}};
    float rgbcam[3][4] = {};
    auto cc = colour::CameraColour::fromLibRaw(camxyz, rgbcam, 3);
    CHECK(cc.valid);

    // A white-balanced neutral lands exactly on working-space white.
    Vec3 w = cc.camToWorking() * Vec3(1, 1, 1);
    CHECK_NEAR(w.x, 1.0, 1e-6); CHECK_NEAR(w.y, 1.0, 1e-6); CHECK_NEAR(w.z, 1.0, 1e-6);

    // A grey card under any illuminant, balanced with the matching multipliers, is neutral.
    for (double T : {2800.0, 4000.0, 5500.0, 6500.0, 9000.0}) {
        Vec3 mul = cc.multipliersFromTempTint(T, 0.0);
        CHECK_NEAR(mul.y, 1.0, 1e-9);
        Vec3 xyz = colour::xyToXYZ(colour::planckianXy(T));
        Vec3 cam = cc.xyzToCam * xyz;
        Vec3 bal = cam * mul;
        Vec3 out = cc.camToWorking() * bal;
        CHECK_NEAR(out.x / out.y, 1.0, 1e-6);
        CHECK_NEAR(out.z / out.y, 1.0, 1e-6);
    }
    // temp/tint <-> multipliers round trip
    for (double T : {2500.0, 3200.0, 5000.0, 8000.0, 15000.0})
        for (double tint : {-40.0, 0.0, 25.0}) {
            Vec3 mul = cc.multipliersFromTempTint(T, tint);
            auto [T2, tint2] = cc.tempTintFromMultipliers(mul);
            CHECK_NEAR(T2, T, T * 0.002);
            CHECK_NEAR(tint2, tint, 0.2);
            Vec3 mul2 = cc.multipliersFromTempTint(T2, tint2);
            CHECK_NEAR(mul2.x, mul.x, 1e-3); CHECK_NEAR(mul2.z, mul.z, 1e-3);
        }
    // Bradford maps the source white to the destination white.
    Vec3 d65 = colour::xyToXYZ(colour::kD65), d50 = colour::xyToXYZ(colour::kD50);
    Vec3 ad = colour::bradford(d65, d50) * d65;
    CHECK_NEAR(ad.x, d50.x, 1e-6); CHECK_NEAR(ad.y, d50.y, 1e-6); CHECK_NEAR(ad.z, d50.z, 1e-6);
    // Working <-> sRGB preserves white and is invertible.
    Vec3 s = colour::workingToLinearSRGB() * Vec3(1, 1, 1);
    CHECK_NEAR(s.x, 1.0, 1e-6); CHECK_NEAR(s.y, 1.0, 1e-6); CHECK_NEAR(s.z, 1.0, 1e-6);
    Vec3 back = colour::linearSRGBToWorking() * (colour::workingToLinearSRGB() * Vec3(0.2, 0.5, 0.9));
    CHECK_NEAR(back.x, 0.2, 1e-9); CHECK_NEAR(back.y, 0.5, 1e-9); CHECK_NEAR(back.z, 0.9, 1e-9);
    // Planckian 6500K sits close to D65.
    auto xy = colour::planckianXy(6504);
    CHECK_NEAR(xy.x, 0.3135, 0.002); CHECK_NEAR(xy.y, 0.3237, 0.002);
    // Fallback camera behaves sanely.
    auto fb = colour::CameraColour::fallbackSRGB();
    Vec3 fw = fb.camToWorking() * Vec3(1, 1, 1);
    CHECK_NEAR(fw.x, 1.0, 1e-6); CHECK_NEAR(fw.z, 1.0, 1e-6);
    auto [Tf, tf] = fb.tempTintFromMultipliers(Vec3(1, 1, 1));
    CHECK_NEAR(Tf, 6500, 150); CHECK_NEAR(tf, 0.0, 20.0);
}

static void testCurve() {
    std::printf("curve\n");
    CurvePoints id;
    auto lut = curve::buildLut(id, 256);
    for (int i = 0; i < 256; ++i) CHECK_NEAR(lut[i], i / 255.0, 1e-6);
    CurvePoints c;
    c.pts = {QPointF(0, 0), QPointF(0.5, 0.6), QPointF(1, 1)};
    auto l2 = curve::buildLut(c, 1024);
    CHECK_NEAR(l2[0], 0.0, 1e-6);
    CHECK_NEAR(l2[1023], 1.0, 1e-6);
    CHECK_NEAR(curve::evaluate(c, 0.5), 0.6, 1e-6);
    for (int i = 1; i < 1024; ++i) CHECK(l2[i] >= l2[i - 1]);
    CHECK_NEAR(curve::parametric(0.3, 0, 0, 0, 0), 0.3, 1e-12);
    CHECK(curve::parametric(0.125, 0, 0, 0, 100) > 0.125);
    CHECK(curve::parametric(0.875, -100, 0, 0, 0) < 0.875);
    ToneParams t;
    CHECK(curve::isIdentity(t));
    t.pShadows = 50;
    CHECK(!curve::isIdentity(t));
    auto rgb = curve::buildRgbLut(t, 256);
    CHECK(rgb.size() == 256 * 3);
    for (int i = 1; i < 256; ++i) CHECK(rgb[i * 3] >= rgb[(i - 1) * 3]);
}

static void testHistory() {
    std::printf("history\n");
    History h(3);
    EditParams a, b, c, d;
    b.tone.exposureEV = 1; c.tone.exposureEV = 2; d.tone.exposureEV = 3;
    h.reset(a);
    CHECK(!h.canUndo() && !h.canRedo());
    h.commit(b); h.commit(c);
    CHECK(h.current() == c);
    CHECK(h.undo() == b);
    CHECK(h.canRedo());
    CHECK(h.redo() == c);
    h.undo();
    h.commit(d);  // truncates redo
    CHECK(!h.canRedo());
    CHECK(h.current() == d);
    h.commit(d);  // identical: no new entry
    CHECK(h.size() == 3);
    h.commit(a);  // capacity 3 drops the oldest
    CHECK(h.size() == 3);
    CHECK(h.undo() == d);
    CHECK(h.undo() == b);
    CHECK(!h.canUndo());
}

static void testGeometry() {
    std::printf("geometry\n");
    std::array<Vec2, 4> q = {Vec2(0.05, 0.02), Vec2(0.97, -0.03), Vec2(1.02, 1.01), Vec2(-0.02, 0.98)};
    Mat3 H = geom::squareToQuad(q);
    const Vec2 unit[4] = {Vec2(0, 0), Vec2(1, 0), Vec2(1, 1), Vec2(0, 1)};
    for (int i = 0; i < 4; ++i) {
        Vec2 m = H.apply(unit[i]);
        CHECK_NEAR(m.x, q[i].x, 1e-9); CHECK_NEAR(m.y, q[i].y, 1e-9);
    }
    Mat3 Hi = H.inverse();
    Vec2 p(0.3, 0.7), r = Hi.apply(H.apply(p));
    CHECK_NEAR(r.x, p.x, 1e-9); CHECK_NEAR(r.y, p.y, 1e-9);

    GeometryParams g;
    g.rotationDeg = 12.5f;
    g.corners[0] = QVector2D(0.04f, 0.01f);
    g.corners[2] = QVector2D(-0.03f, 0.02f);
    g.perspVertical = 30;
    auto fg = geom::FrameGeometry::compute(g, 4000, 3000);
    for (Vec2 s : {Vec2(0.1, 0.2), Vec2(0.5, 0.5), Vec2(0.9, 0.95)}) {
        Vec2 f = fg.lensToFrame(s), b = fg.frameToLens(f);
        CHECK_NEAR(b.x, s.x, 1e-9); CHECK_NEAR(b.y, s.y, 1e-9);
    }
    // rotation only: a point on the +x axis moves down on screen for a clockwise rotation
    GeometryParams rot; rot.rotationDeg = 90;
    auto fr = geom::FrameGeometry::compute(rot, 1000, 1000);
    Vec2 m = fr.lensToFrame(Vec2(1.0, 0.5));
    CHECK_NEAR(m.x, 0.5, 1e-9); CHECK_NEAR(m.y, 1.0, 1e-9);

    Vec2 same = geom::manualDistortion(Vec2(0.8, 0.3), 1.5, 0, 0, 0, 1.0);
    CHECK_NEAR(same.x, 0.8, 1e-12); CHECK_NEAR(same.y, 0.3, 1e-12);
    Vec2 centre = geom::manualDistortion(Vec2(0.5, 0.5), 1.5, 0.1, -0.2, 0.05, 1.0);
    CHECK_NEAR(centre.x, 0.5, 1e-12); CHECK_NEAR(centre.y, 0.5, 1e-12);
    // c>0 (barrel correction) samples closer to the centre: r_src < r_dst
    Vec2 corner = geom::manualDistortion(Vec2(0.9, 0.9), 1.0, 0, 0, 0.1, 1.0);
    CHECK(corner.x < 0.9 && corner.y < 0.9);

    // largest inscribed rectangle inside a disc
    int W = 200, Hh = 150;
    std::vector<uint8_t> mask(size_t(W) * Hh, 0);
    for (int y = 0; y < Hh; ++y) for (int x = 0; x < W; ++x)
        mask[size_t(y) * W + x] = std::hypot(x - 100.0, y - 75.0) < 70.0 ? 1 : 0;
    QRect free = geom::largestInscribedRect(mask, W, Hh, 0);
    CHECK(free.width() > 80 && free.height() > 80);
    QRect sq = geom::largestInscribedRect(mask, W, Hh, 1.0);
    CHECK(std::abs(sq.width() - sq.height()) <= 1);
    CHECK(sq.width() >= 96 && sq.width() <= 100);  // inscribed square of a radius-70 disc: side 99
    for (QRect r : {free, sq})
        for (int y = r.top(); y <= r.bottom(); ++y) for (int x = r.left(); x <= r.right(); ++x) CHECK(mask[size_t(y) * W + x]);
    QRect wide = geom::largestInscribedRect(mask, W, Hh, 3.0);
    CHECK(std::abs(wide.width() - 3 * wide.height()) <= 3);
}

static void testLens() {
    std::printf("lens\n");
    LensGrid id = LensGrid::identity(17);
    CHECK(id.valid());
    Vec2 s = id.sample(1, Vec2(0.3, 0.8));
    CHECK_NEAR(s.x, 0.3, 1e-6); CHECK_NEAR(s.y, 0.8, 1e-6);

    LensModel lm;
    CHECK(lm.databaseLoaded());
    LensQuery q;
    q.cameraMake = "SONY"; q.cameraModel = "ILCE-7C"; q.lensModel = "E PZ 18-105mm F4 G OSS";
    q.focal = 30; q.aperture = 4; q.cropFactorHint = 1.5;
    bool ok = lm.match(q);
    std::printf("  lensfun match: %s (%s)\n", ok ? "yes" : "no", lm.statusText().toUtf8().constData());
    if (ok) {
        CHECK(lm.hasDistortion());
        LensGrid g = lm.buildGrid(3968, 2648, true, true, 65);
        CHECK(g.valid());
        Vec2 c = g.sample(1, Vec2(0.5, 0.5));
        CHECK_NEAR(c.x, 0.5, 2e-3); CHECK_NEAR(c.y, 0.5, 2e-3);
        Vec2 corner = g.sample(1, Vec2(0.02, 0.02));
        CHECK(std::abs(corner.x - 0.02) > 1e-4 || std::abs(corner.y - 0.02) > 1e-4);
        Vec2 r = g.sample(0, Vec2(0.1, 0.1)), b = g.sample(2, Vec2(0.1, 0.1));
        CHECK(std::abs(r.x - b.x) > 1e-7);  // TCA separates the channels
        std::printf("  grid: centre=(%.5f,%.5f) corner(0.02,0.02)->(%.5f,%.5f)\n", c.x, c.y, corner.x, corner.y);
    }
    LensQuery bad;
    bad.cameraMake = "Nobody"; bad.cameraModel = "Nothing"; bad.lensModel = "Imaginary 1mm";
    CHECK(!lm.match(bad));
    CHECK(!lm.hasProfile());
}

static void testIcc() {
    std::printf("icc (lcms)\n");
    icc::Profile srgb = icc::createProfile(icc::Space::SRGB);
    CHECK(srgb != nullptr);
    // Display LUT built from the sRGB profile must be the identity for greys (input axes are sRGB-encoded).
    const int n = 9;
    std::vector<float> lut = icc::buildDisplayLut(srgb.get(), n);
    CHECK(lut.size() == size_t(n) * n * n * 3);
    for (int i = 0; i < n; ++i) {
        size_t k = ((size_t(i) * n + i) * n + i) * 3;
        double e = double(i) / (n - 1);
        CHECK_NEAR(lut[k], e, 2e-3); CHECK_NEAR(lut[k + 1], e, 2e-3); CHECK_NEAR(lut[k + 2], e, 2e-3);
    }
    // An in-gamut colour through the export transform matches the analytic matrix + OETF path.
    icc::Transform xf(srgb.get());
    CHECK(xf.valid());
    Vec3 lin(0.5, 0.2, 0.1);
    Vec3 work = colour::linearSRGBToWorking() * lin;
    float in[3] = {float(work.x), float(work.y), float(work.z)}, out[3];
    xf.apply(in, out, 1);
    auto oetf = [](double x) { return x <= 0.0031308 ? 12.92 * x : 1.055 * std::pow(x, 1 / 2.4) - 0.055; };
    CHECK_NEAR(out[0], oetf(lin.x), 2e-3); CHECK_NEAR(out[1], oetf(lin.y), 2e-3); CHECK_NEAR(out[2], oetf(lin.z), 2e-3);
    // Every output profile serialises and describes itself.
    for (int i = 0; i < icc::kSpaceCount; ++i) {
        icc::Profile p = icc::createProfile(icc::Space(i));
        CHECK(p != nullptr);
        CHECK(icc::profileBytes(p.get()).size() > 200);
        CHECK(!icc::profileDescription(p.get()).isEmpty());
    }
}

int main() {
    testIcc();
    testColour();
    testCurve();
    testHistory();
    testGeometry();
    testLens();
    if (failures) { std::printf("%d failure(s)\n", failures); return 1; }
    std::printf("all core tests passed\n");
    return 0;
}
