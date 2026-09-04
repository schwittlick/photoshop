#pragma once
// Colour science for the pipeline:
//   camera RGB --[inverse cam_xyz, D65-normalised]--> XYZ D65 --[Bradford]--> XYZ D50
//              --[Rec.2020 primaries, D50 adapted]--> working space (linear)
// plus the correlated-colour-temperature machinery behind the temp/tint sliders.
#include "core/Math.h"
#include <utility>

namespace re::colour {

struct Chromaticity { double x, y; };

extern const Chromaticity kD65;
extern const Chromaticity kD50;
extern const Chromaticity kRec2020[3];   // R, G, B
extern const Chromaticity kSRGB[3];
extern const Chromaticity kAdobeRGB[3];
extern const Chromaticity kDisplayP3[3];

Vec3 xyToXYZ(Chromaticity c, double Y = 1.0);
Vec2 xyToUv(Chromaticity c);               // CIE 1960 uv
Chromaticity uvToXy(Vec2 uv);

// RGB -> XYZ matrix for given primaries and white (white maps to XYZ with Y=1).
Mat3 rgbToXyz(const Chromaticity prim[3], Chromaticity white);
// Bradford chromatic adaptation from one white (XYZ) to another.
Mat3 bradford(const Vec3& srcWhiteXYZ, const Vec3& dstWhiteXYZ);

// Working space helpers (linear Rec.2020, D50-adapted as ICC does it).
Mat3 workingToXyzD50();
Mat3 xyzD50ToWorking();
Mat3 workingToLinearSRGB();     // relative colorimetric (white -> white)
Mat3 linearSRGBToWorking();
Mat3 workingToLinear(const Chromaticity prim[3]);  // to another D65 RGB space, relative

// Planckian locus chromaticity (Kim et al. approximation), 1667K..25000K.
Chromaticity planckianXy(double kelvin);

constexpr double kTempMin = 2000.0;
constexpr double kTempMax = 25000.0;
constexpr double kTintScale = 3000.0;   // tint units per unit of Delta-uv (ACR/DNG-like scale)

// Everything derived from the camera's colour matrix.
struct CameraColour {
    Mat3 xyzToCam;          // LibRaw cam_xyz (XYZ D65 -> camera), 3 colours
    bool valid = false;

    // Camera (white balanced, neutral = equal channels) -> working space.
    Mat3 camToWorking() const;
    // Green-normalised camera-space multipliers that neutralise an illuminant described by temp/tint.
    Vec3 multipliersFromTempTint(double kelvin, double tint) const;
    // Inverse of the above (numerically exact to the solver tolerance).
    std::pair<double, double> tempTintFromMultipliers(const Vec3& mul) const;

    // Build from LibRaw data. rgbCam is used as a fallback when camXyz is empty.
    static CameraColour fromLibRaw(const float camXyz[4][3], const float rgbCam[3][4], int colours);
    static CameraColour fallbackSRGB();
};

}  // namespace re::colour
