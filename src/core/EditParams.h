#pragma once
// The single source of truth for a render. Render output is a pure function of
// (source image, EditParams, target resolution). All geometry is expressed in
// normalised frame coordinates (0..1 across the oriented source frame) so the
// proxy preview and the full-resolution export agree exactly.
#include <QPointF>
#include <QRectF>
#include <QVector2D>
#include <array>
#include <vector>

namespace re {

struct CurvePoints {
    std::vector<QPointF> pts{QPointF(0, 0), QPointF(1, 1)};  // sorted by x, within [0,1]
    bool isIdentity() const;
    bool operator==(const CurvePoints& o) const { return pts == o.pts; }
};

struct WhiteBalance {
    float temp = 5000.f;  // correlated colour temperature in kelvin (describes the illuminant)
    float tint = 0.f;     // ACR convention: positive = illuminant greener -> image pushed magenta
    bool operator==(const WhiteBalance&) const = default;
};

struct ToneParams {
    float exposureEV = 0.f;  // -5..+5
    float contrast = 0.f;    // -100..100
    float highlights = 0.f;  // -100..100
    float shadows = 0.f;     // -100..100
    float blacks = 0.f;      // -100..100
    float whites = 0.f;      // -100..100
    float saturation = 0.f;  // -100..100
    float vibrance = 0.f;    // -100..100
    // parametric curve regions
    float pHighlights = 0.f, pLights = 0.f, pDarks = 0.f, pShadows = 0.f;  // -100..100
    CurvePoints curveMaster, curveR, curveG, curveB;
    bool operator==(const ToneParams&) const = default;
};

struct LensParams {
    bool lensAuto = true;              // use the lensfun profile when one was matched
    float profileDistortion = 100.f;   // 0..200 % of the profile's distortion correction
    float profileVignetting = 100.f;   // 0..200 %
    bool profileCA = true;             // apply the profile's TCA correction
    // manual ptlens: r_src = r_dst * (a r^3 + b r^2 + c r + d), d = 1-a-b-c, r normalised to half the diagonal
    float distA = 0.f, distB = 0.f, distC = 0.f;
    float caRed = 0.f, caBlue = 0.f;   // radial scale of the R/B sampling position, in 0.01% units (-100..100)
    float vignetteAmount = 0.f;        // -100..100, manual, applied on top of the profile
    float vignetteMidpoint = 50.f;     // 0..100
    bool operator==(const LensParams&) const = default;
};

struct GeometryParams {
    float rotationDeg = 0.f;                         // positive = clockwise on screen, -45..45
    std::array<QVector2D, 4> corners{};              // perspective handles: normalised offsets TL, TR, BR, BL
    float perspVertical = 0.f, perspHorizontal = 0.f;  // -100..100 keystone sliders, composed with corners
    QRectF cropNorm{0, 0, 1, 1};                     // normalised crop in the corrected frame
    bool operator==(const GeometryParams&) const = default;
};

struct EditParams {
    WhiteBalance wb;
    ToneParams tone;
    LensParams lens;
    GeometryParams geom;
    float outputSharpenAmount = 0.f;  // 0..100, unsharp mask applied last on export
    float outputSharpenRadius = 0.8f; // pixels
    bool operator==(const EditParams&) const = default;
};

}  // namespace re
