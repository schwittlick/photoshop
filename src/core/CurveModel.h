#pragma once
// Tone curve evaluation: monotone cubic (PCHIP) point curve plus ACR-style
// parametric regions, baked into per-channel LUTs sampled by the GPU.
#include "core/EditParams.h"
#include <array>
#include <vector>

namespace re::curve {

constexpr int kLutSize = 1024;

// Evaluate the point curve at x in [0,1].
double evaluate(const CurvePoints& pts, double x);
// Full LUT of the point curve.
std::vector<float> buildLut(const CurvePoints& pts, int n = kLutSize);
// Parametric curve (highlights/lights/darks/shadows in -100..100) evaluated at x.
double parametric(double x, float highlights, float lights, float darks, float shadows);
// Interleaved RGB LUT (n*3 floats): channel -> parametric -> master -> channel curve, monotone-enforced.
std::vector<float> buildRgbLut(const ToneParams& tone, int n = kLutSize);
bool isIdentity(const ToneParams& tone);

// Insert/replace a point, keeping x order; returns the index of the point.
int insertPoint(CurvePoints& pts, QPointF p);

}  // namespace re::curve
