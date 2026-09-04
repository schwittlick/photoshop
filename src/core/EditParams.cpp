#include "core/EditParams.h"
#include <cmath>

namespace re {

bool CurvePoints::isIdentity() const {
    if (pts.size() < 2) return true;
    for (const QPointF& p : pts)
        if (std::abs(p.x() - p.y()) > 1e-6) return false;
    return true;
}

}  // namespace re
