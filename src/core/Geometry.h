#pragma once
// Geometry of the single warp pass. Everything is in normalised frame coordinates
// (0..1 across the oriented source frame). Conventions shared with warp.comp:
//   output pixel -> crop rect -> frame coords q
//   q --rotate^-1--> pre-rotation frame --persp^-1--> lens-corrected frame --lens--> source
#include "core/EditParams.h"
#include "core/Math.h"
#include <QRect>
#include <array>
#include <cstdint>
#include <vector>

namespace re::geom {

// Homography mapping the unit square (0,0),(1,0),(1,1),(0,1) onto the quad (same order).
Mat3 squareToQuad(const std::array<Vec2, 4>& quad);
// Perspective quad = unit square + handle offsets + keystone sliders.
std::array<Vec2, 4> perspectiveQuad(const GeometryParams& g);

struct FrameGeometry {
    double aspect = 1.0;  // W/H of the oriented source
    double cosR = 1.0, sinR = 0.0;
    Mat3 persp, invPersp;
    QRectF crop{0, 0, 1, 1};

    static FrameGeometry compute(const GeometryParams& g, int W, int H);

    Vec2 rotateInv(Vec2 q) const;  // frame -> pre-rotation frame
    Vec2 rotateFwd(Vec2 q) const;
    Vec2 frameToLens(Vec2 q) const;  // inverse chain used by the shader
    Vec2 lensToFrame(Vec2 s) const;  // forward chain
};

// Manual ptlens distortion plus per-channel radial scale (CA); r normalised to half the diagonal.
// Keep in sync with manualDist() in warp.comp.
Vec2 manualDistortion(Vec2 q, double aspect, double a, double b, double c, double chScale);

// Largest axis-aligned rectangle of non-zero cells in a mask. `aspect` is the wanted
// width/height in mask pixels; <= 0 means unconstrained. Empty QRect when nothing fits.
QRect largestInscribedRect(const std::vector<uint8_t>& mask, int w, int h, double aspect);

}  // namespace re::geom
