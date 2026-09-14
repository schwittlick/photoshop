#include "core/AutoTone.h"
#include <algorithm>
#include <cmath>

namespace re::autotone {

namespace {

double percentile(const std::array<uint32_t, 256>& bins, uint32_t total, double q) {
    const uint64_t need = std::max<uint64_t>(1, uint64_t(std::ceil(q * total)));
    uint64_t acc = 0;
    for (int i = 0; i < 256; ++i) {
        acc += bins[i];
        if (acc >= need) return (i + 0.5) / 255.0;
    }
    return 1.0;
}

}  // namespace

Stats statsFrom(const HistogramData& h) {
    Stats s;
    if (!h.valid || h.total == 0) return s;
    double sum = 0;
    uint64_t low = 0, high = 0;
    for (int i = 0; i < 256; ++i) {
        sum += double(h.l[i]) * (i / 255.0);
        if (i < 16) low += h.l[i];    // below 0.06
        if (i > 239) high += h.l[i];  // above 0.94
    }
    s.mean = sum / h.total;
    s.median = percentile(h.l, h.total, 0.5);
    s.p05 = percentile(h.l, h.total, 0.005);
    s.p10 = percentile(h.l, h.total, 0.10);
    s.p90 = percentile(h.l, h.total, 0.90);
    s.p995 = percentile(h.l, h.total, 0.995);
    s.lowMass = double(low) / h.total;
    s.highMass = double(high) / h.total;
    s.clipHigh = double(h.clippedHigh) / h.total;
    s.clipLow = double(h.clippedLow) / h.total;
    s.valid = true;
    return s;
}

EditParams solve(RenderBackend& be, int slot, const ViewSpec& view, const EditParams& start, const Targets& T, int* renders) {
    EditParams p = start;
    ToneParams& t = p.tone;
    t.exposureEV = 0; t.contrast = 0; t.highlights = 0; t.shadows = 0; t.whites = 0; t.blacks = 0;
    RenderOptions o;
    o.histogram = true;
    o.output = OutputMode::DisplaySRGB;
    int count = 0;
    auto measure = [&]() {
        HistogramData h;
        be.render(slot, view, p, o, &h);
        ++count;
        return statsFrom(h);
    };
    // Finds the value in [lo, hi] where err() crosses zero; err must increase with the field. When the
    // target is out of reach the nearer bound is kept, which is also how "nothing to do" comes out.
    auto bisect = [&](float& field, float lo, float hi, float tol, auto err) {
        field = lo;
        if (err() >= 0) return;
        field = hi;
        if (err() <= 0) return;
        for (int i = 0; i < 10 && hi - lo > tol; ++i) {
            field = 0.5f * (lo + hi);
            double e = err();
            if (std::abs(e) < 1e-4) { lo = hi = field; break; }
            (e < 0 ? lo : hi) = field;
        }
        field = 0.5f * (lo + hi);
    };

    const Stats s0 = measure();
    if (!s0.valid) return start;
    const double midTarget = s0.mid() + T.midPull * (T.mid - s0.mid());
    const double spread0 = s0.p90 - s0.p10;
    const double spreadTarget = spread0 + T.spreadPull * (T.spread - spread0);

    auto exposure = [&] { bisect(t.exposureEV, -T.maxExposure, T.maxExposure, 0.005f, [&] { return measure().mid() - midTarget; }); };
    auto highlights = [&] {
        bisect(t.highlights, -80, 0, 0.5f, [&] { Stats s = measure(); return std::max(s.highMass - T.highlightMass, s.clipHigh - T.highlightClip); });
    };
    auto shadows = [&] { bisect(t.shadows, 0, 70, 0.5f, [&] { return T.shadowMass - measure().lowMass; }); };
    auto blacks = [&] { bisect(t.blacks, -100, 100, 0.5f, [&] { return measure().p05 - T.black; }); };
    auto contrast = [&] { bisect(t.contrast, 0, T.maxContrast, 0.5f, [&] { Stats s = measure(); return (s.p90 - s.p10) - spreadTarget; }); };

    exposure(); highlights(); shadows(); blacks(); contrast();
    // The sliders interact; a second pass settles the brightness and the ends on top of the rest.
    exposure(); highlights(); shadows(); blacks();

    t.exposureEV = std::round(t.exposureEV * 100) / 100;
    t.contrast = std::round(t.contrast);
    t.highlights = std::round(t.highlights);
    t.shadows = std::round(t.shadows);
    t.blacks = std::round(t.blacks);
    if (renders) *renders = count;
    return p;
}

}  // namespace re::autotone
