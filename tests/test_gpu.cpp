// GPU tests (offscreen context): proxy/full-res geometric agreement (spec §3.2),
// undo exactness, histogram consistency.
#include "core/Geometry.h"
#include "gpu/GLBackend.h"
#include "io/RawImage.h"
#include "io/Exporter.h"
#include <QGuiApplication>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QSurfaceFormat>
#include <QElapsedTimer>
#include <QOpenGLFunctions>
#include <cmath>
#include <cstdio>

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); ++failures; } } while (0)

using namespace re;

static RawImage makeSynthetic(int W, int H) {
    RawImage img;
    img.width = W;
    img.height = H;
    img.rgb.resize(size_t(W) * H * 3);
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            bool line = (x % 64) < 3 || (y % 64) < 3;
            uint16_t base = uint16_t(8000 + 20000.0 * x / W);
            uint16_t v = line ? 60000 : base;
            uint16_t* p = &img.rgb[(size_t(y) * W + x) * 3];
            p[0] = v; p[1] = uint16_t(line ? 60000 : base / 2); p[2] = uint16_t(line ? 60000 : base / 3);
        }
    img.camera = colour::CameraColour::fallbackSRGB();
    return img;
}

int main(int argc, char** argv) {
    QSurfaceFormat f;
    f.setVersion(4, 3);
    f.setProfile(QSurfaceFormat::CoreProfile);
    QSurfaceFormat::setDefaultFormat(f);
    QGuiApplication app(argc, argv);
    QOpenGLContext ctx;
    ctx.setFormat(f);
    if (!ctx.create()) { std::printf("SKIP: no OpenGL context\n"); return 0; }
    QOffscreenSurface surf;
    surf.setFormat(ctx.format());
    surf.create();
    if (!ctx.makeCurrent(&surf)) { std::printf("SKIP: cannot make context current\n"); return 0; }

    GLBackend be;
    QString err;
    if (!be.initialize(&err)) { std::printf("FAIL: backend init: %s\n", err.toUtf8().constData()); return 1; }
    std::printf("backend: %s\n", be.info().toUtf8().constData());

    const int W = 2048, H = 1536;
    RawImage img = makeSynthetic(W, H);
    be.setSource(img);

    EditParams p;
    p.lens.distC = 40;   // barrel correction
    p.lens.distA = 10;
    p.lens.caRed = 20;
    p.geom.rotationDeg = 7;
    p.geom.corners[0] = QVector2D(0.03f, 0.02f);
    p.geom.corners[1] = QVector2D(-0.02f, 0.01f);
    p.geom.perspHorizontal = 15;
    p.geom.cropNorm = QRectF(0.1, 0.1, 0.75, 0.75);  // 1536 x 1152 at full res

    // ---- Test 1: warp coordinates agree between full res and 1/4 res proxy within one source pixel.
    std::printf("proxy vs full-res geometry\n");
    RenderOptions dbg;
    dbg.debugCoords = true;
    ViewSpec full{1536, 1152, QRectF(0, 0, 1, 1), p.geom.cropNorm, 0};
    ViewSpec quarter{384, 288, QRectF(0, 0, 1, 1), p.geom.cropNorm, 2};
    std::vector<float> a, b;
    TextureHandle ta = be.render(RenderBackend::SlotCanvas, full, p, dbg);
    CHECK(ta && be.readback(ta, full.outWidth, full.outHeight, a));
    TextureHandle tb = be.render(RenderBackend::SlotAux, quarter, p, dbg);
    CHECK(tb && be.readback(tb, quarter.outWidth, quarter.outHeight, b));
    double maxErr = 0, sumErr = 0;
    long n = 0;
    for (int j = 0; j < quarter.outHeight; ++j)
        for (int i = 0; i < quarter.outWidth; ++i) {
            const float* q = &b[(size_t(j) * quarter.outWidth + i) * 4];
            if (q[3] < 0.99f) continue;
            // quarter pixel centre lies between full pixels 4i+1 and 4i+2: average their coordinates
            double fu = 0, fv = 0, fa = 1;
            for (int dy = 1; dy <= 2; ++dy) for (int dx = 1; dx <= 2; ++dx) {
                const float* fp = &a[(size_t(4 * j + dy) * full.outWidth + 4 * i + dx) * 4];
                fu += fp[0] * 0.25; fv += fp[1] * 0.25; fa = std::min(fa, double(fp[3]));
            }
            if (fa < 0.99) continue;
            double e = std::hypot((fu - q[0]) * W, (fv - q[1]) * H);
            maxErr = std::max(maxErr, e);
            sumErr += e;
            ++n;
        }
    std::printf("  compared %ld pixels: max error %.4f px, mean %.5f px\n", n, maxErr, n ? sumErr / n : 0.0);
    CHECK(n > 50000);
    CHECK(maxErr < 1.0);

    // ---- Test 2: the rendered pictures agree too (box-filtered full res vs proxy render).
    std::printf("proxy vs full-res pixels\n");
    RenderOptions disp;
    disp.output = OutputMode::DisplaySRGB;
    disp.floatOutput = true;
    std::vector<float> fa2, qa2;
    CHECK(be.readback(be.render(RenderBackend::SlotCanvas, full, p, disp), full.outWidth, full.outHeight, fa2));
    CHECK(be.readback(be.render(RenderBackend::SlotAux, quarter, p, disp), quarter.outWidth, quarter.outHeight, qa2));
    double diff = 0; long cnt = 0; long big = 0;
    for (int j = 0; j < quarter.outHeight; ++j)
        for (int i = 0; i < quarter.outWidth; ++i) {
            const float* q = &qa2[(size_t(j) * quarter.outWidth + i) * 4];
            if (q[3] < 0.99f) continue;
            double box[3] = {0, 0, 0};
            for (int dy = 0; dy < 4; ++dy) for (int dx = 0; dx < 4; ++dx) {
                const float* fp = &fa2[(size_t(4 * j + dy) * full.outWidth + 4 * i + dx) * 4];
                for (int c = 0; c < 3; ++c) box[c] += fp[c] / 16.0;
            }
            double d = (std::abs(box[0] - q[0]) + std::abs(box[1] - q[1]) + std::abs(box[2] - q[2])) / 3.0;
            diff += d; ++cnt;
            if (d > 0.25) ++big;
        }
    std::printf("  mean abs diff %.4f, %.3f%% pixels differ by more than 0.25\n", cnt ? diff / cnt : 0, cnt ? 100.0 * big / cnt : 0);
    CHECK(cnt > 0 && diff / cnt < 0.03);
    CHECK(cnt > 0 && double(big) / cnt < 0.01);

    // ---- Test 3: undo returns bit-identical output.
    std::printf("undo exactness\n");
    RenderOptions d8;
    ViewSpec view{800, 600, QRectF(0, 0, 1, 1), p.geom.cropNorm, 1};
    std::vector<float> r1, r2;
    EditParams p2 = p;
    p2.tone.exposureEV = 1.3f; p2.tone.contrast = 40; p2.wb.temp = 3800; p2.geom.rotationDeg = -3; p2.lens.distC = 0;
    p2.tone.curveMaster.pts = {QPointF(0, 0), QPointF(0.4, 0.5), QPointF(1, 1)};
    CHECK(be.readback(be.render(RenderBackend::SlotCanvas, view, p, d8), 800, 600, r1));
    be.render(RenderBackend::SlotCanvas, view, p2, d8);
    CHECK(be.readback(be.render(RenderBackend::SlotCanvas, view, p, d8), 800, 600, r2));
    CHECK(r1 == r2);
    // and the other direction, through a fresh slot
    std::vector<float> r3, r4;
    CHECK(be.readback(be.render(RenderBackend::SlotCanvas, view, p2, d8), 800, 600, r3));
    CHECK(be.readback(be.render(RenderBackend::SlotAux, view, p2, d8), 800, 600, r4));
    CHECK(r3 == r4);
    CHECK(r1 != r3);

    // ---- Test 4: histogram counts every covered pixel exactly once.
    std::printf("histogram\n");
    RenderOptions hopt;
    hopt.histogram = true;
    HistogramData hist;
    ViewSpec hv{512, 384, QRectF(0, 0, 1, 1), QRectF(0, 0, 1, 1), 2};
    std::vector<float> hpix;
    CHECK(be.readback(be.render(RenderBackend::SlotHistogram, hv, p, hopt, &hist), 512, 384, hpix));
    CHECK(hist.valid);
    uint64_t sr = 0, sg = 0, sb = 0, sl = 0;
    for (int i = 0; i < 256; ++i) { sr += hist.r[i]; sg += hist.g[i]; sb += hist.b[i]; sl += hist.l[i]; }
    uint32_t covered = 0;
    for (int i = 0; i < 512 * 384; ++i) if (hpix[size_t(i) * 4 + 3] > 0.5f) ++covered;
    std::printf("  total %u covered %u clippedHigh %u clippedLow %u\n", hist.total, covered, hist.clippedHigh, hist.clippedLow);
    CHECK(hist.total == covered && covered > 100000);
    CHECK(sr == hist.total && sg == hist.total && sb == hist.total && sl == hist.total);

    // ---- Test 5: identity geometry maps output pixel centres exactly onto source pixel centres.
    std::printf("identity mapping\n");
    EditParams idp;
    ViewSpec idv{W, H, QRectF(0, 0, 1, 1), QRectF(0, 0, 1, 1), 0};
    std::vector<float> idc;
    CHECK(be.readback(be.render(RenderBackend::SlotAux, idv, idp, dbg), W, H, idc));
    double idErr = 0;
    for (int y = 0; y < H; y += 97) for (int x = 0; x < W; x += 89) {
        const float* c = &idc[(size_t(y) * W + x) * 4];
        idErr = std::max(idErr, std::abs(c[0] * W - (x + 0.5)) + std::abs(c[1] * H - (y + 0.5)));
    }
    std::printf("  max centre error %.5f px\n", idErr);
    CHECK(idErr < 1e-2);

    // ---- Test 6: a neutral grey card survives the colour chain at the expected level.
    // LibRaw (highlight=1) stores white-balanced data scaled by 1/max(mul); a stored grey v must come out as v*max(mul).
    std::printf("neutral grey level\n");
    {
        float camxyz[4][3] = {{0.5271f, -0.0712f, -0.0347f}, {-0.6153f, 1.3653f, 0.2763f}, {-0.1601f, 0.2366f, 0.7242f}, {0, 0, 0}};
        float rgbcam[3][4] = {};
        RawImage grey;
        grey.width = grey.height = 64;
        grey.camera = colour::CameraColour::fromLibRaw(camxyz, rgbcam, 3);
        Vec3 mul(2.1875, 1.0, 2.0234);
        double mx = mul.maxComponent();
        grey.preMul[0] = float(mul.x / mx); grey.preMul[1] = float(mul.y / mx); grey.preMul[2] = float(mul.z / mx);
        const double v = 0.2;
        grey.rgb.assign(64 * 64 * 3, uint16_t(std::lround(v * 65535)));
        be.setSource(grey);
        EditParams gp;
        auto [T, tint] = grey.camera.tempTintFromMultipliers(mul);
        gp.wb.temp = float(T); gp.wb.tint = float(tint);
        RenderOptions lin;
        lin.output = OutputMode::WorkingLinear;
        lin.floatOutput = true;
        std::vector<float> g;
        ViewSpec gv{64, 64, QRectF(0, 0, 1, 1), QRectF(0, 0, 1, 1), 0};
        CHECK(be.readback(be.render(RenderBackend::SlotAux, gv, gp, lin), 64, 64, g));
        const float* c = &g[(32 * 64 + 32) * 4];
        std::printf("  working rgb = %.5f %.5f %.5f, expected %.5f\n", c[0], c[1], c[2], v * mx);
        CHECK(std::abs(c[0] - v * mx) < 2e-3 && std::abs(c[1] - v * mx) < 2e-3 && std::abs(c[2] - v * mx) < 2e-3);
        RenderOptions srgb;
        srgb.floatOutput = true;
        std::vector<float> e;
        CHECK(be.readback(be.render(RenderBackend::SlotAux, gv, gp, srgb), 64, 64, e));
        const float* ec = &e[(32 * 64 + 32) * 4];
        double lin_v = v * mx;
        double expectEnc = lin_v <= 0.0031308 ? 12.92 * lin_v : 1.055 * std::pow(lin_v, 1 / 2.4) - 0.055;
        std::printf("  sRGB encoded = %.5f %.5f %.5f, expected %.5f\n", ec[0], ec[1], ec[2], expectEnc);
        CHECK(std::abs(ec[0] - expectEnc) < 3e-3 && std::abs(ec[1] - expectEnc) < 3e-3 && std::abs(ec[2] - expectEnc) < 3e-3);
    }

    // ---- Test 6b: preview sharpening acts on edges only and matches the CPU unsharp mask.
    std::printf("preview sharpening\n");
    {
        be.setSource(img);
        EditParams sp;
        ViewSpec sv{512, 384, QRectF(0, 0, 1, 1), QRectF(0, 0, 1, 1), 2};
        RenderOptions plain, sharp;
        sharp.sharpenScale = 1.f;
        sp.outputSharpenAmount = 80;
        sp.outputSharpenRadius = 1.0f;
        std::vector<float> a, b;
        CHECK(be.readback(be.render(RenderBackend::SlotAux, sv, sp, plain), 512, 384, a));
        CHECK(be.readback(be.render(RenderBackend::SlotAux, sv, sp, sharp), 512, 384, b));
        CHECK(a != b);
        // CPU reference: same mask on the plain render
        std::vector<float> rgb(size_t(512) * 384 * 3);
        for (size_t i = 0; i < size_t(512) * 384; ++i) for (int c = 0; c < 3; ++c) rgb[i * 3 + c] = a[i * 4 + c];
        Exporter::unsharpMask(rgb, 512, 384, sp.outputSharpenAmount, sp.outputSharpenRadius);
        double maxd = 0, sumd = 0;
        for (size_t i = 0; i < size_t(512) * 384; ++i) for (int c = 0; c < 3; ++c) {
            double d = std::abs(rgb[i * 3 + c] - b[i * 4 + c]);
            maxd = std::max(maxd, d); sumd += d;
        }
        std::printf("  GPU vs CPU unsharp mask: mean %.5f max %.4f (8-bit step = 0.0039)\n", sumd / (512.0 * 384 * 3), maxd);
        CHECK(maxd < 0.02 && sumd / (512.0 * 384 * 3) < 0.002);
        // sub-pixel radius at small zoom: preview leaves the image alone, like the export would at that size
        RenderOptions tiny;
        tiny.sharpenScale = 0.1f;
        std::vector<float> c;
        CHECK(be.readback(be.render(RenderBackend::SlotAux, sv, sp, tiny), 512, 384, c));
        CHECK(c == a);
    }

    // ---- Test 7: interactive cost on a 45 MP source (viewport-sized passes, so it must not scale with the file).
    std::printf("45 MP timing\n");
    {
        RawImage big = makeSynthetic(8192, 5504);
        QElapsedTimer t;
        t.start();
        be.setSource(big);
        auto* gl = QOpenGLContext::currentContext()->functions();
        gl->glFinish();
        std::printf("  upload + mip chain: %lld ms\n", t.elapsed());
        EditParams bp;
        bp.lens.distC = 20; bp.geom.rotationDeg = 3;
        ViewSpec view{2560, 1440, QRectF(0, 0, 1, 1), QRectF(0, 0, 1, 1), 1};
        RenderOptions o;
        o.histogram = true;
        HistogramData hd;
        be.render(RenderBackend::SlotCanvas, view, bp, o, &hd);
        gl->glFinish();
        auto bench = [&](const char* what, auto mutate) {
            t.restart();
            const int n = 20;
            for (int i = 0; i < n; ++i) { mutate(i); be.render(RenderBackend::SlotCanvas, view, bp, o, &hd); }
            gl->glFinish();
            double ms = double(t.elapsed()) / n;
            std::printf("  %-32s %.1f ms/frame\n", what, ms);
            return ms;
        };
        double tone = bench("tone slider (stage 3 only)", [&](int i) { bp.tone.exposureEV = 0.05f * i; });
        double wb = bench("white balance (all stages)", [&](int i) { bp.wb.temp = 4000 + 50 * i; });
        double geo = bench("rotation (warp + tone)", [&](int i) { bp.geom.rotationDeg = 0.1f * i; });
        CHECK(tone < 200 && wb < 400 && geo < 300);
        // full-res export tile cost
        t.restart();
        ViewSpec tile{1024, 1024, QRectF(0, 0, 0.125, 0.186), QRectF(0, 0, 1, 1), 0};
        RenderOptions ex; ex.output = OutputMode::WorkingLinear; ex.floatOutput = true;
        std::vector<float> px;
        be.readback(be.render(RenderBackend::SlotAux, tile, bp, ex), 1024, 1024, px);
        std::printf("  first export tile incl. full-res colour stage: %lld ms\n", t.elapsed());
    }

    if (failures) { std::printf("%d failure(s)\n", failures); return 1; }
    std::printf("all gpu tests passed\n");
    return 0;
}
