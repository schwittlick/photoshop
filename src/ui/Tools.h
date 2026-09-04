#pragma once
// Shared UI enums (kept free of widget dependencies so panels can signal them).
namespace re {

enum class Tool { Hand = 0, Crop, Straighten, WhiteBalance, Perspective };
enum class CropAspect { Free = 0, Original, Square, R4x3, R3x2, R16x9 };

// Constraint aspect as width/height in pixels, oriented like the frame; 0 = unconstrained.
inline double cropAspectValue(CropAspect a, double frameAspect, bool swapped) {
    double r = 0;
    switch (a) {
        case CropAspect::Free: return 0;
        case CropAspect::Original: r = frameAspect; break;
        case CropAspect::Square: r = 1; break;
        case CropAspect::R4x3: r = 4.0 / 3.0; break;
        case CropAspect::R3x2: r = 1.5; break;
        case CropAspect::R16x9: r = 16.0 / 9.0; break;
    }
    if (a != CropAspect::Original && frameAspect < 1) r = 1 / r;
    if (swapped) r = 1 / r;
    return r;
}

}  // namespace re
