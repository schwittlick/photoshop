#include "core/CurveModel.h"
#include "core/Math.h"
#include <algorithm>
#include <cmath>

namespace re::curve {

namespace {

// Fritsch-Carlson monotone slopes.
std::vector<double> pchipSlopes(const std::vector<QPointF>& p) {
    size_t n = p.size();
    std::vector<double> d(n, 0.0);
    if (n < 2) return d;
    std::vector<double> h(n - 1), delta(n - 1);
    for (size_t i = 0; i + 1 < n; ++i) {
        h[i] = std::max(1e-9, p[i + 1].x() - p[i].x());
        delta[i] = (p[i + 1].y() - p[i].y()) / h[i];
    }
    if (n == 2) { d[0] = d[1] = delta[0]; return d; }
    for (size_t i = 1; i + 1 < n; ++i) {
        if (delta[i - 1] * delta[i] <= 0) { d[i] = 0; continue; }
        double w1 = 2 * h[i] + h[i - 1], w2 = h[i] + 2 * h[i - 1];
        d[i] = (w1 + w2) / (w1 / delta[i - 1] + w2 / delta[i]);
    }
    auto endSlope = [](double h0, double h1, double del0, double del1) {
        double d0 = ((2 * h0 + h1) * del0 - h0 * del1) / (h0 + h1);
        if (d0 * del0 <= 0) d0 = 0;
        else if (del0 * del1 <= 0 && std::abs(d0) > std::abs(3 * del0)) d0 = 3 * del0;
        return d0;
    };
    d[0] = endSlope(h[0], h[1], delta[0], delta[1]);
    d[n - 1] = endSlope(h[n - 2], h[n - 3], delta[n - 2], delta[n - 3]);
    return d;
}

}  // namespace

double evaluate(const CurvePoints& cp, double x) {
    const auto& p = cp.pts;
    if (p.empty()) return x;
    if (p.size() == 1) return p[0].y();
    if (x <= p.front().x()) return p.front().y();
    if (x >= p.back().x()) return p.back().y();
    std::vector<double> d = pchipSlopes(p);
    size_t i = 0;
    while (i + 2 < p.size() && x > p[i + 1].x()) ++i;
    double h = std::max(1e-9, p[i + 1].x() - p[i].x());
    double t = (x - p[i].x()) / h;
    double t2 = t * t, t3 = t2 * t;
    double h00 = 2 * t3 - 3 * t2 + 1, h10 = t3 - 2 * t2 + t, h01 = -2 * t3 + 3 * t2, h11 = t3 - t2;
    return h00 * p[i].y() + h10 * h * d[i] + h01 * p[i + 1].y() + h11 * h * d[i + 1];
}

std::vector<float> buildLut(const CurvePoints& pts, int n) {
    std::vector<float> lut(n);
    if (pts.pts.size() < 2) {
        for (int i = 0; i < n; ++i) lut[i] = float(i) / float(n - 1);
        return lut;
    }
    std::vector<double> d = pchipSlopes(pts.pts);
    const auto& p = pts.pts;
    size_t seg = 0;
    for (int i = 0; i < n; ++i) {
        double x = double(i) / double(n - 1);
        double y;
        if (x <= p.front().x()) y = p.front().y();
        else if (x >= p.back().x()) y = p.back().y();
        else {
            while (seg + 2 < p.size() && x > p[seg + 1].x()) ++seg;
            double h = std::max(1e-9, p[seg + 1].x() - p[seg].x());
            double t = (x - p[seg].x()) / h, t2 = t * t, t3 = t2 * t;
            y = (2 * t3 - 3 * t2 + 1) * p[seg].y() + (t3 - 2 * t2 + t) * h * d[seg]
              + (-2 * t3 + 3 * t2) * p[seg + 1].y() + (t3 - t2) * h * d[seg + 1];
        }
        lut[i] = float(clampd(y, 0.0, 1.0));
    }
    return lut;
}

double parametric(double x, float highlights, float lights, float darks, float shadows) {
    // Raised-cosine bumps centred on the four quarters, damped at the endpoints so 0 and 1 stay fixed.
    auto bump = [](double x, double c) {
        double d = std::abs(x - c);
        if (d >= 0.25) return 0.0;
        return 0.5 * (1.0 + std::cos(kPi * d / 0.25));
    };
    const double amp = 0.10;
    double damp = std::min(1.0, 4.0 * x * (1.0 - x) * 1.5);
    double y = x + amp * damp * (shadows / 100.0 * bump(x, 0.125) + darks / 100.0 * bump(x, 0.375)
                                 + lights / 100.0 * bump(x, 0.625) + highlights / 100.0 * bump(x, 0.875));
    return clampd(y, 0.0, 1.0);
}

bool isIdentity(const ToneParams& t) {
    return t.pHighlights == 0 && t.pLights == 0 && t.pDarks == 0 && t.pShadows == 0
        && t.curveMaster.isIdentity() && t.curveR.isIdentity() && t.curveG.isIdentity() && t.curveB.isIdentity();
}

std::vector<float> buildRgbLut(const ToneParams& tone, int n) {
    std::vector<float> master = buildLut(tone.curveMaster, n);
    std::vector<float> ch[3] = {buildLut(tone.curveR, n), buildLut(tone.curveG, n), buildLut(tone.curveB, n)};
    std::vector<float> out(size_t(n) * 3);
    auto sampleLut = [n](const std::vector<float>& lut, double x) {
        double f = clampd(x, 0.0, 1.0) * (n - 1);
        int i = int(f);
        int j = std::min(i + 1, n - 1);
        double t = f - i;
        return lut[i] * (1 - t) + lut[j] * t;
    };
    for (int c = 0; c < 3; ++c) {
        float prev = 0.f;
        for (int i = 0; i < n; ++i) {
            double x = double(i) / double(n - 1);
            double y = parametric(x, tone.pHighlights, tone.pLights, tone.pDarks, tone.pShadows);
            y = sampleLut(master, y);
            y = sampleLut(ch[c], y);
            float v = float(clampd(y, 0.0, 1.0));
            if (i > 0 && v < prev) v = prev;  // enforce monotonicity
            prev = v;
            out[size_t(i) * 3 + c] = v;
        }
    }
    return out;
}

int insertPoint(CurvePoints& cp, QPointF p) {
    p.setX(clampd(p.x(), 0.0, 1.0));
    p.setY(clampd(p.y(), 0.0, 1.0));
    auto& v = cp.pts;
    auto it = std::lower_bound(v.begin(), v.end(), p.x(), [](const QPointF& a, double x) { return a.x() < x; });
    it = v.insert(it, p);
    return int(it - v.begin());
}

}  // namespace re::curve
