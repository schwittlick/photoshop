#pragma once
// Decoded raw: camera-native linear RGB, demosaiced and oriented by LibRaw, plus
// everything the colour stage needs to undo LibRaw's white balance pre-scaling.
#include "core/ColourMath.h"
#include <QString>
#include <cstdint>
#include <vector>

namespace re {

struct RawImage {
    int width = 0, height = 0;        // oriented dimensions
    std::vector<uint16_t> rgb;        // interleaved RGB16, row-major, oriented

    // LibRaw applied pre_mul (normalised so max = 1, highlight mode "unclip") before
    // demosaic. Channel c therefore clips at preMul[c] in the 0..1 data, and
    // value / preMul[c] recovers the normalised raw (clip at 1.0 for every channel).
    float preMul[4] = {1.f, 1.f, 1.f, 1.f};
    float camMul[4] = {0.f, 0.f, 0.f, 0.f};  // as-shot raw multipliers as reported by the camera
    colour::CameraColour camera;
    bool colourIsSRGBFallback = false;  // LibRaw converted to linear sRGB itself (4-colour sensors)

    unsigned black = 0, maximum = 0;
    int flip = 0;
    QString make, model, lens;
    double focal = 0, aperture = 0, shutter = 0;
    int iso = 0, focal35 = 0;

    double aspect() const { return height > 0 ? double(width) / height : 1.0; }
    Vec3 asShotMultipliers() const {
        double g = preMul[1] > 0 ? preMul[1] : 1.0;
        return {preMul[0] / g, 1.0, preMul[2] / g};
    }
    // Normalised raw (clip = 1) of a pixel, averaged over a small window.
    Vec3 sampleRaw(int x, int y, int radius) const;
};

}  // namespace re
