#pragma once
// lensfun integration. The database is the source of truth for the profile models;
// they are evaluated on the CPU into a coarse displacement grid (distortion + TCA)
// and a radial gain LUT (vignetting) that the shaders interpolate.
#include "core/Math.h"
#include <QString>
#include <memory>
#include <vector>

namespace re {

struct LensQuery {
    QString cameraMake, cameraModel, lensModel;
    double focal = 0, aperture = 0, distance = 0;
    double cropFactorHint = 0;  // e.g. focal35 / focal, catches crop-mode shots
};

struct LensGrid {
    int n = 0;
    double margin = 0.25;     // the grid covers [-margin, 1+margin]^2 of the lens-corrected frame
    std::vector<float> uv;    // [layer][j][i][2], layers R,G,B: normalised source coords
    bool valid() const { return n > 1 && uv.size() == size_t(n) * n * 6; }
    static LensGrid identity(int n = 129);
    Vec2 sample(int channel, Vec2 q) const;  // bilinear, q in lens-corrected frame coords
};

struct VignetteLut {
    std::vector<float> gain;  // gain vs radius (0 = centre, 1 = corner)
    bool valid() const { return !gain.empty(); }
};

class LensModel {
public:
    LensModel();
    ~LensModel();
    LensModel(const LensModel&) = delete;
    LensModel& operator=(const LensModel&) = delete;

    bool databaseLoaded() const;
    bool match(const LensQuery& q);
    void clear();

    bool hasProfile() const;
    bool hasDistortion() const;
    bool hasTCA() const;
    bool hasVignetting() const;
    QString cameraName() const;
    QString lensName() const;
    QString statusText() const;

    LensGrid buildGrid(int W, int H, bool distortion, bool tca, int n = 129) const;
    VignetteLut buildVignette(int W, int H, int n = 256) const;

private:
    struct Impl;
    std::unique_ptr<Impl> d_;
};

}  // namespace re
