#include "core/EditParams.h"
#include <cmath>

namespace re {

bool CurvePoints::isIdentity() const {
    if (pts.size() < 2) return true;
    for (const QPointF& p : pts)
        if (std::abs(p.x() - p.y()) > 1e-6) return false;
    return true;
}

EditParams applySync(const EditParams& src, const EditParams& dst, const SyncMask& m) {
    EditParams out = dst;
    if (m.whiteBalance) out.wb = src.wb;
    if (m.tone) {
        out.tone.exposureEV = src.tone.exposureEV;
        out.tone.contrast = src.tone.contrast;
        out.tone.highlights = src.tone.highlights;
        out.tone.shadows = src.tone.shadows;
        out.tone.blacks = src.tone.blacks;
        out.tone.whites = src.tone.whites;
        out.tone.saturation = src.tone.saturation;
        out.tone.vibrance = src.tone.vibrance;
    }
    if (m.curves) {
        out.tone.pHighlights = src.tone.pHighlights;
        out.tone.pLights = src.tone.pLights;
        out.tone.pDarks = src.tone.pDarks;
        out.tone.pShadows = src.tone.pShadows;
        out.tone.curveMaster = src.tone.curveMaster;
        out.tone.curveR = src.tone.curveR;
        out.tone.curveG = src.tone.curveG;
        out.tone.curveB = src.tone.curveB;
    }
    if (m.lens) out.lens = src.lens;
    if (m.rotation) out.geom.rotationDeg = src.geom.rotationDeg;
    if (m.perspective) {
        out.geom.corners = src.geom.corners;
        out.geom.perspVertical = src.geom.perspVertical;
        out.geom.perspHorizontal = src.geom.perspHorizontal;
        out.geom.guides = src.geom.guides;
    }
    if (m.crop) out.geom.cropNorm = src.geom.cropNorm;
    if (m.outputSharpen) {
        out.outputSharpenAmount = src.outputSharpenAmount;
        out.outputSharpenRadius = src.outputSharpenRadius;
    }
    return out;
}

}  // namespace re
