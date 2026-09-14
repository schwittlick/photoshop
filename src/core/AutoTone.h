#pragma once
// "Auto" tone: solves Exposure, Blacks, Highlights, Shadows and Contrast from the histogram of the rendered
// crop, the way Camera Raw's Auto worked before it became a neural network. The renderer is a black box:
// every slider is bisected against a target measured on a small display-referred render, so the solve needs
// no knowledge of the slider maths and accounts for whatever curves are already set. Whites is reset to 0
// and not solved: in this pipeline it is a plain gain, the same lever as Exposure, and solving both makes
// them fight; Exposure, Highlights and Contrast own the bright end instead.
#include "core/EditParams.h"
#include "gpu/RenderBackend.h"
#include <cstdint>

namespace re::autotone {

struct Targets {
    float mid = 0.45f;             // encoded luminance the blend of mean and median is pulled towards (18 % grey encodes to 0.46)
    float midPull = 0.8f;          // fraction of the distance to `mid` that Exposure covers; 1 would normalise every scene to medium
    float maxExposure = 3.f;       // |EV| bound
    float black = 0.02f;           // encoded level for the 0.5th percentile
    float white = 0.95f;           // encoded level for the 99.5th percentile
    float highlightMass = 0.02f;   // Highlights pulls until at most this fraction of pixels sits above 0.94 ...
    float highlightClip = 0.003f;  // ... and at most this fraction is clipped
    float shadowMass = 0.03f;      // Shadows lifts until at most this fraction of pixels sits below 0.06
    float spread = 0.60f;          // p90 - p10 of a "normal" scene; Contrast raises a flatter image towards it, never lowers
    float spreadPull = 0.5f;       // fraction of the distance to `spread` that Contrast covers
    float maxContrast = 35.f;
};

struct Stats {
    double mean = 0, median = 0, p05 = 0, p10 = 0, p90 = 0, p995 = 0;  // encoded luminance 0..1
    double lowMass = 0, highMass = 0, clipHigh = 0, clipLow = 0;      // fractions of the covered pixels
    bool valid = false;
    double mid() const { return 0.5 * (mean + median); }
};
Stats statsFrom(const HistogramData& h);

// Renders into `slot` about a hundred times (tone stage only, a few ms in total for a 640 px view). The
// caller owns the GL context and releases the slot afterwards. Returns `start` with the six tone sliders
// replaced; everything else, including curves and colour, is left as it is.
EditParams solve(RenderBackend& backend, int slot, const ViewSpec& view, const EditParams& start, const Targets& targets = {}, int* renders = nullptr);

}  // namespace re::autotone
