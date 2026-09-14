// Unit tests for the CPU side: colour maths, curves, history, geometry, lensfun wrapper.
#include "core/ColourMath.h"
#include "core/CurveModel.h"
#include "core/Geometry.h"
#include "core/History.h"
#include "core/LensModel.h"
#include "core/OutputProfiles.h"
#include "io/Sidecar.h"
#include <QFile>
#include <QJsonArray>
#include <QTemporaryDir>
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

static void testSync() {
    std::printf("sync\n");
    EditParams src;
    src.wb.temp = 3200; src.wb.tint = 12;
    src.tone.exposureEV = 1.5f; src.tone.contrast = 30; src.tone.vibrance = 20;
    src.tone.pLights = 15;
    src.tone.curveMaster.pts = {QPointF(0, 0), QPointF(0.4, 0.3), QPointF(1, 1)};
    src.lens.lensAuto = false; src.lens.distC = 5;
    src.geom.rotationDeg = 2.5f; src.geom.perspVertical = 20; src.geom.corners[1] = QVector2D(0.05f, -0.02f);
    src.geom.cropNorm = QRectF(0.1, 0.1, 0.5, 0.6);
    src.outputSharpenAmount = 70;
    EditParams dst;
    dst.wb.temp = 5600; dst.geom.cropNorm = QRectF(0.2, 0.0, 0.7, 0.7); dst.geom.rotationDeg = -1;

    // Defaults: tone, curves, lens and output sharpening travel; the frame-specific groups stay.
    EditParams out = applySync(src, dst, SyncMask());
    CHECK(out.tone.exposureEV == 1.5f && out.tone.contrast == 30 && out.tone.vibrance == 20);
    CHECK(out.tone.pLights == 15 && out.tone.curveMaster == src.tone.curveMaster);
    CHECK(out.lens == src.lens);
    CHECK(out.outputSharpenAmount == 70);
    CHECK(out.wb == dst.wb);
    CHECK(out.geom.cropNorm == dst.geom.cropNorm);
    CHECK(out.geom.rotationDeg == -1 && out.geom.perspVertical == 0 && out.geom.corners[1] == QVector2D());
    SyncMask all;
    all.whiteBalance = all.rotation = all.perspective = all.crop = true;
    CHECK(applySync(src, dst, all) == src);
    SyncMask none;
    none.tone = none.curves = none.lens = none.outputSharpen = false;
    CHECK(!none.any());
    CHECK(applySync(src, dst, none) == dst);
    // Tone and curves are separate groups: syncing only the curves leaves the exposure alone.
    SyncMask curvesOnly = none;
    curvesOnly.curves = true;
    out = applySync(src, dst, curvesOnly);
    CHECK(out.tone.exposureEV == 0.f && out.tone.curveMaster == src.tone.curveMaster && out.tone.pLights == 15);
}

static void testGuidedUpright() {
    std::printf("guided upright\n");
    const int W = 1500, H = 1000;
    const double aspect = double(W) / H;
    // A rectangle in the scene (metric coordinates) photographed through a keystone plus a tilt: the guides
    // are what the camera saw, in frame coordinates.
    Mat3 G;  // scene -> photo (metric)
    {
        Mat3 P; P.m[2][0] = 0.18; P.m[2][1] = -0.22;
        Mat3 R; double th = 4.0 * kPi / 180; R.m[0][0] = std::cos(th); R.m[0][1] = -std::sin(th); R.m[1][0] = std::sin(th); R.m[1][1] = std::cos(th);
        G = R * P;
    }
    auto photo = [&](double mx, double my) { Vec2 q = G.apply(Vec2(mx, my)); return QPointF(q.x / aspect + 0.5, q.y + 0.5); };
    Guide v1{photo(-0.3, -0.25), photo(-0.3, 0.25), true}, v2{photo(0.3, -0.2), photo(0.3, 0.3), true};
    Guide h1{photo(-0.35, -0.2), photo(0.35, -0.2), false}, h2{photo(-0.3, 0.2), photo(0.4, 0.2), false};
    auto check = [&](const std::vector<Guide>& guides, double rotationDeg, bool expectOk, const char* what) {
        std::array<QVector2D, 4> corners;
        QString why;
        bool ok = geom::solveGuidedUpright(guides, aspect, rotationDeg, &corners, &why);
        if (ok != expectOk) std::printf("  %s: unexpected %s (%s)\n", what, ok ? "success" : "failure", why.toUtf8().constData());
        CHECK(ok == expectOk);
        if (!ok) return;
        GeometryParams g;
        g.rotationDeg = float(rotationDeg);
        g.corners = corners;
        geom::FrameGeometry fg = geom::FrameGeometry::compute(g, W, H);
        double worst = 0;
        for (const Guide& gd : guides) {
            Vec2 a = fg.lensToFrame(Vec2(gd.a.x(), gd.a.y())), b = fg.lensToFrame(Vec2(gd.b.x(), gd.b.y()));
            double err = gd.vertical ? std::abs(a.x - b.x) * W : std::abs(a.y - b.y) * H;  // pixels at full resolution
            worst = std::max(worst, err);
        }
        Vec2 c = fg.lensToFrame(Vec2(0.5, 0.5));
        std::printf("  %s: worst deviation %.5f px, centre moved %.5f px\n", what, worst, std::hypot((c.x - 0.5) * W, (c.y - 0.5) * H));
        CHECK(worst < 1e-3);
        CHECK(std::hypot((c.x - 0.5) * W, (c.y - 0.5) * H) < 1e-3);
    };
    check({v1, v2}, 0, true, "two verticals");
    check({h1, h2}, 0, true, "two horizontals");
    check({v1, h1}, 0, true, "one of each");
    check({v1, v2, h1, h2}, 0, true, "two of each");
    check({v1, v2, h1}, 0, true, "two verticals and a horizontal");
    check({v1, v2, h1, h2}, 5.0, true, "two of each with an existing rotation");
    check({v1}, 0, false, "one guide");
    // Guides crossing inside the frame (an X) cannot be made parallel.
    Guide x1{QPointF(0.2, 0.1), QPointF(0.8, 0.9), true}, x2{QPointF(0.8, 0.1), QPointF(0.2, 0.9), true};
    check({x1, x2}, 0, false, "guides crossing inside the frame");
    // Already-straight guides are a no-op: the corners stay at zero.
    Guide s1{QPointF(0.2, 0.1), QPointF(0.2, 0.9), true}, s2{QPointF(0.8, 0.1), QPointF(0.8, 0.9), true};
    std::array<QVector2D, 4> corners;
    CHECK(geom::solveGuidedUpright({s1, s2}, aspect, 0, &corners, nullptr));
    for (const QVector2D& c : corners) CHECK(std::abs(c.x()) < 1e-9 && std::abs(c.y()) < 1e-9);
}

static void testSidecar() {
    std::printf("sidecar\n");
    EditParams p;  // every field away from its default
    p.wb.temp = 3210; p.wb.tint = -7.5f;
    p.tone.exposureEV = -0.35f; p.tone.contrast = 12; p.tone.highlights = -40; p.tone.shadows = 33; p.tone.whites = 5; p.tone.blacks = -9;
    p.tone.saturation = 4; p.tone.vibrance = 18; p.tone.pHighlights = 1; p.tone.pLights = 2; p.tone.pDarks = 3; p.tone.pShadows = 4;
    p.tone.curveMaster.pts = {QPointF(0, 0), QPointF(0.25, 0.2), QPointF(0.75, 0.8), QPointF(1, 1)};
    p.tone.curveR.pts = {QPointF(0, 0.02), QPointF(1, 0.98)};
    p.tone.curveG.pts = {QPointF(0, 0), QPointF(0.5, 0.55), QPointF(1, 1)};
    p.tone.curveB.pts = {QPointF(0, 0), QPointF(1, 0.9)};
    p.lens.lensAuto = false; p.lens.profileDistortion = 80; p.lens.profileVignetting = 120; p.lens.profileCA = false;
    p.lens.distA = 0.5f; p.lens.distB = -1.5f; p.lens.distC = 2.5f; p.lens.caRed = -3; p.lens.caBlue = 4; p.lens.vignetteAmount = -25; p.lens.vignetteMidpoint = 60;
    p.geom.rotationDeg = -1.75f; p.geom.perspVertical = 22; p.geom.perspHorizontal = -8;
    p.geom.corners = {QVector2D(0.01f, 0.02f), QVector2D(-0.03f, 0.04f), QVector2D(0.05f, -0.06f), QVector2D(-0.07f, -0.08f)};
    p.geom.cropNorm = QRectF(0.1, 0.2, 0.6, 0.5);
    p.geom.guides = {Guide{QPointF(0.2, 0.1), QPointF(0.25, 0.9), true}, Guide{QPointF(0.1, 0.3), QPointF(0.9, 0.35), false}};
    p.outputSharpenAmount = 45; p.outputSharpenRadius = 1.3f;

    // JSON round trip is exact.
    EditParams back;
    QString err;
    CHECK(sidecar::fromJson(sidecar::toJson(p), &back, &err));
    CHECK(back == p);
    // Missing keys keep what the caller passed in (the image's defaults); unknown keys are ignored.
    EditParams defaults;
    defaults.wb.temp = 4711;
    QJsonObject partial{{"format", "photoshop-edit"}, {"version", 1}, {"tone", QJsonObject{{"exposure", 1.0}, {"future", 3}}}, {"other", true}};
    EditParams got = defaults;
    CHECK(sidecar::fromJson(partial, &got, &err));
    CHECK(got.wb.temp == 4711 && got.tone.exposureEV == 1.0f && got.geom.cropNorm == QRectF(0, 0, 1, 1));
    // Wrong format, a newer version and a broken curve are rejected without touching the output.
    EditParams untouched = defaults;
    CHECK(!sidecar::fromJson(QJsonObject{{"format", "something-else"}, {"version", 1}}, &untouched, &err) && untouched == defaults);
    QJsonObject newer = sidecar::toJson(p);
    newer["version"] = sidecar::kVersion + 1;
    CHECK(!sidecar::fromJson(newer, &untouched, &err) && untouched == defaults);
    QJsonObject badCurve = sidecar::toJson(p);
    {
        QJsonObject tone = badCurve["tone"].toObject(), curves = tone["curves"].toObject();
        curves["master"] = QJsonArray{QJsonArray{0, 0}, QJsonArray{2, 1}};
        tone["curves"] = curves;
        badCurve["tone"] = tone;
    }
    CHECK(!sidecar::fromJson(badCurve, &untouched, &err) && untouched == defaults);
    // File round trip next to a (pretend) raw: absent, written, read back exactly, unreadable, removed.
    QTemporaryDir tmp;
    CHECK(tmp.isValid());
    const QString raw = tmp.filePath("DSC00001.ARW");
    CHECK(sidecar::pathFor(raw) == raw + ".json");
    EditParams loaded = defaults;
    CHECK(sidecar::read(raw, &loaded, &err) == sidecar::ReadResult::None);
    CHECK(sidecar::write(raw, p, &err));
    CHECK(sidecar::read(raw, &loaded, &err) == sidecar::ReadResult::Loaded && loaded == p);
    {
        QFile f(sidecar::pathFor(raw));
        CHECK(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write("{ not json");
    }
    loaded = defaults;
    CHECK(sidecar::read(raw, &loaded, &err) == sidecar::ReadResult::Invalid && loaded == defaults);
    CHECK(sidecar::remove(raw, &err) && !QFile::exists(sidecar::pathFor(raw)));
    CHECK(sidecar::remove(raw, &err));
}

int main() {
    testIcc();
    testColour();
    testCurve();
    testHistory();
    testSync();
    testSidecar();
    testGuidedUpright();
    testGeometry();
    testLens();
    if (failures) { std::printf("%d failure(s)\n", failures); return 1; }
    std::printf("all core tests passed\n");
    return 0;
}
