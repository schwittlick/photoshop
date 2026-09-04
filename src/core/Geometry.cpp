#include "core/Geometry.h"
#include <algorithm>
#include <cmath>

namespace re::geom {

Mat3 squareToQuad(const std::array<Vec2, 4>& p) {
    // Heckbert, "Fundamentals of Texture Mapping and Image Warping".
    double sx = p[0].x - p[1].x + p[2].x - p[3].x;
    double sy = p[0].y - p[1].y + p[2].y - p[3].y;
    Mat3 M;
    if (std::abs(sx) < 1e-12 && std::abs(sy) < 1e-12) {
        M.m[0][0] = p[1].x - p[0].x; M.m[0][1] = p[2].x - p[1].x; M.m[0][2] = p[0].x;
        M.m[1][0] = p[1].y - p[0].y; M.m[1][1] = p[2].y - p[1].y; M.m[1][2] = p[0].y;
        M.m[2][0] = 0; M.m[2][1] = 0; M.m[2][2] = 1;
        return M;
    }
    double dx1 = p[1].x - p[2].x, dx2 = p[3].x - p[2].x;
    double dy1 = p[1].y - p[2].y, dy2 = p[3].y - p[2].y;
    double det = dx1 * dy2 - dx2 * dy1;
    if (std::abs(det) < 1e-15) det = 1e-15;
    double g = (sx * dy2 - dx2 * sy) / det;
    double h = (dx1 * sy - sx * dy1) / det;
    M.m[0][0] = p[1].x - p[0].x + g * p[1].x; M.m[0][1] = p[3].x - p[0].x + h * p[3].x; M.m[0][2] = p[0].x;
    M.m[1][0] = p[1].y - p[0].y + g * p[1].y; M.m[1][1] = p[3].y - p[0].y + h * p[3].y; M.m[1][2] = p[0].y;
    M.m[2][0] = g; M.m[2][1] = h; M.m[2][2] = 1;
    return M;
}

std::array<Vec2, 4> perspectiveQuad(const GeometryParams& g) {
    std::array<Vec2, 4> q = {Vec2(0, 0), Vec2(1, 0), Vec2(1, 1), Vec2(0, 1)};
    const double s = 0.25;
    double v = g.perspVertical / 100.0 * s, hz = g.perspHorizontal / 100.0 * s;
    // vertical keystone: positive widens the top edge; horizontal: positive widens the left edge
    q[0].x -= v; q[1].x += v; q[3].x += v; q[2].x -= v;
    q[0].y -= hz; q[3].y += hz; q[1].y += hz; q[2].y -= hz;
    for (int i = 0; i < 4; ++i) { q[i].x += g.corners[i].x(); q[i].y += g.corners[i].y(); }
    return q;
}

FrameGeometry FrameGeometry::compute(const GeometryParams& g, int W, int H) {
    FrameGeometry f;
    f.aspect = H > 0 ? double(W) / double(H) : 1.0;
    double th = g.rotationDeg * kPi / 180.0;
    f.cosR = std::cos(th);
    f.sinR = std::sin(th);
    f.persp = squareToQuad(perspectiveQuad(g));
    f.invPersp = f.persp.inverse();
    f.crop = g.cropNorm;
    return f;
}

Vec2 FrameGeometry::rotateInv(Vec2 q) const {
    Vec2 c((q.x - 0.5) * aspect, q.y - 0.5);
    Vec2 r(c.x * cosR + c.y * sinR, -c.x * sinR + c.y * cosR);
    return {r.x / aspect + 0.5, r.y + 0.5};
}

Vec2 FrameGeometry::rotateFwd(Vec2 q) const {
    Vec2 c((q.x - 0.5) * aspect, q.y - 0.5);
    Vec2 r(c.x * cosR - c.y * sinR, c.x * sinR + c.y * cosR);
    return {r.x / aspect + 0.5, r.y + 0.5};
}

Vec2 FrameGeometry::frameToLens(Vec2 q) const { return invPersp.apply(rotateInv(q)); }
Vec2 FrameGeometry::lensToFrame(Vec2 s) const { return rotateFwd(persp.apply(s)); }

Vec2 manualDistortion(Vec2 q, double aspect, double a, double b, double c, double chScale) {
    double hdl = 0.5 * std::sqrt(aspect * aspect + 1.0);
    Vec2 cen((q.x - 0.5) * aspect / hdl, (q.y - 0.5) / hdl);
    double r = cen.length();
    double d = 1.0 - a - b - c;
    double s = ((a * r + b) * r + c) * r + d;
    cen = cen * (s * chScale);
    return {cen.x * hdl / aspect + 0.5, cen.y * hdl + 0.5};
}

namespace {

QRect largestFree(const std::vector<uint8_t>& mask, int w, int h) {
    std::vector<int> height(w, 0), stack;
    QRect best;
    long bestArea = 0;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) height[x] = mask[size_t(y) * w + x] ? height[x] + 1 : 0;
        stack.clear();
        for (int x = 0; x <= w; ++x) {
            int cur = x < w ? height[x] : 0;
            while (!stack.empty() && height[stack.back()] >= cur) {
                int hh = height[stack.back()];
                stack.pop_back();
                int left = stack.empty() ? 0 : stack.back() + 1;
                long area = long(hh) * (x - left);
                if (area > bestArea) { bestArea = area; best = QRect(left, y - hh + 1, x - left, hh); }
            }
            stack.push_back(x);
        }
    }
    return best;
}

}  // namespace

QRect largestInscribedRect(const std::vector<uint8_t>& mask, int w, int h, double aspect) {
    if (w <= 0 || h <= 0 || mask.size() < size_t(w) * h) return {};
    if (aspect <= 0) return largestFree(mask, w, h);

    // 2D prefix sums of the mask.
    std::vector<int> ps(size_t(w + 1) * (h + 1), 0);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            ps[size_t(y + 1) * (w + 1) + x + 1] = ps[size_t(y) * (w + 1) + x + 1] + ps[size_t(y + 1) * (w + 1) + x]
                                                 - ps[size_t(y) * (w + 1) + x] + (mask[size_t(y) * w + x] ? 1 : 0);
    auto sum = [&](int x0, int y0, int x1, int y1) {  // half-open
        return ps[size_t(y1) * (w + 1) + x1] - ps[size_t(y0) * (w + 1) + x1] - ps[size_t(y1) * (w + 1) + x0] + ps[size_t(y0) * (w + 1) + x0];
    };
    auto feasible = [&](int rh, QRect* out) {
        int rw = std::max(1, int(std::lround(rh * aspect)));
        if (rw > w || rh > h) return false;
        double cx = w * 0.5, cy = h * 0.5, bestD = 1e30;
        bool ok = false;
        for (int y = 0; y + rh <= h; ++y)
            for (int x = 0; x + rw <= w; ++x)
                if (sum(x, y, x + rw, y + rh) == rw * rh) {
                    double d = std::hypot(x + rw * 0.5 - cx, y + rh * 0.5 - cy);
                    if (d < bestD) { bestD = d; *out = QRect(x, y, rw, rh); ok = true; }
                }
        return ok;
    };
    int lo = 0, hi = std::min(h, int(w / aspect));
    QRect best;
    while (lo < hi) {
        int mid = (lo + hi + 1) / 2;
        QRect r;
        if (feasible(mid, &r)) { lo = mid; best = r; }
        else hi = mid - 1;
    }
    return best;
}

}  // namespace re::geom
