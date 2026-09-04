#include "core/LensModel.h"
#include <lensfun/lensfun.h>
#include <QDebug>
#include <algorithm>
#include <cmath>

namespace re {

LensGrid LensGrid::identity(int n) {
    LensGrid g;
    g.n = n;
    g.uv.resize(size_t(n) * n * 6);
    for (int layer = 0; layer < 3; ++layer)
        for (int j = 0; j < n; ++j)
            for (int i = 0; i < n; ++i) {
                size_t k = ((size_t(layer) * n + j) * n + i) * 2;
                g.uv[k] = float(-g.margin + (1.0 + 2.0 * g.margin) * i / (n - 1));
                g.uv[k + 1] = float(-g.margin + (1.0 + 2.0 * g.margin) * j / (n - 1));
            }
    return g;
}

Vec2 LensGrid::sample(int ch, Vec2 q) const {
    if (!valid()) return q;
    double fx = clampd((q.x + margin) / (1.0 + 2.0 * margin), 0.0, 1.0) * (n - 1);
    double fy = clampd((q.y + margin) / (1.0 + 2.0 * margin), 0.0, 1.0) * (n - 1);
    int i0 = std::min(int(fx), n - 2), j0 = std::min(int(fy), n - 2);
    double tx = fx - i0, ty = fy - j0;
    auto at = [&](int i, int j) {
        size_t k = ((size_t(ch) * n + j) * n + i) * 2;
        return Vec2(uv[k], uv[k + 1]);
    };
    Vec2 a = at(i0, j0) * (1 - tx) + at(i0 + 1, j0) * tx;
    Vec2 b = at(i0, j0 + 1) * (1 - tx) + at(i0 + 1, j0 + 1) * tx;
    return a * (1 - ty) + b * ty;
}

struct LensModel::Impl {
    lfDatabase* db = nullptr;
    bool loaded = false;
    const lfCamera* cam = nullptr;
    const lfLens* lens = nullptr;
    float focal = 50.f, aperture = 8.f, distance = 1000.f, crop = 1.f;
    QString status;
};

LensModel::LensModel() : d_(new Impl) {
    d_->db = new lfDatabase();
    lfError e = d_->db->Load();
    d_->loaded = (e == LF_NO_ERROR);
    if (!d_->loaded) d_->status = QStringLiteral("lensfun database not found");
}

LensModel::~LensModel() {
    delete d_->db;
}

bool LensModel::databaseLoaded() const { return d_->loaded; }

void LensModel::clear() {
    d_->cam = nullptr;
    d_->lens = nullptr;
    d_->status.clear();
}

bool LensModel::match(const LensQuery& q) {
    clear();
    if (!d_->loaded) { d_->status = QStringLiteral("lensfun database not found"); return false; }
    QByteArray make = q.cameraMake.trimmed().toUtf8(), model = q.cameraModel.trimmed().toUtf8();
    QByteArray lens = q.lensModel.trimmed().toUtf8();

    if (!model.isEmpty()) {
        const lfCamera** cams = d_->db->FindCamerasExt(make.isEmpty() ? nullptr : make.constData(), model.constData(), LF_SEARCH_LOOSE);
        if (cams && cams[0]) d_->cam = cams[0];
        lf_free(cams);
    }
    if (!lens.isEmpty()) {
        // 1. strict search restricted to the camera (mount + crop compatible);
        // 2. strict search without the camera (catches crop-mode shots with APS-C glass on full frame);
        // 3. loose search, but only accept convincing scores (garbage queries score ~20-35, real matches 70+).
        struct Attempt { const lfCamera* cam; int flags; int minScore; };
        const Attempt attempts[] = {{d_->cam, 0, 0}, {nullptr, 0, 0}, {d_->cam, LF_SEARCH_LOOSE, 60}, {nullptr, LF_SEARCH_LOOSE, 60}};
        for (const Attempt& at : attempts) {
            if (d_->lens) break;
            const lfLens** lenses = d_->db->FindLenses(at.cam, nullptr, lens.constData(), at.flags);
            if (!lenses) continue;
            for (int i = 0; lenses[i]; ++i) {
                const lfLens* l = lenses[i];
                if (l->Score < at.minScore) continue;
                bool calib = (l->CalibDistortion && l->CalibDistortion[0]) || (l->CalibTCA && l->CalibTCA[0])
                          || (l->CalibVignetting && l->CalibVignetting[0]);
                if (calib) { d_->lens = l; break; }
            }
            lf_free(lenses);
        }
    }
    if (!d_->lens) {
        d_->status = d_->cam ? QStringLiteral("camera found, lens \"%1\" not in database").arg(q.lensModel)
                             : QStringLiteral("no profile for \"%1\"").arg(q.lensModel.isEmpty() ? QStringLiteral("unknown lens") : q.lensModel);
        return false;
    }
    d_->focal = q.focal > 0 ? float(q.focal) : d_->lens->MinFocal;
    d_->aperture = q.aperture > 0 ? float(q.aperture) : std::max(d_->lens->MinAperture, 8.f);
    d_->distance = q.distance > 0 ? float(q.distance) : 1000.f;
    if (q.cropFactorHint > 0.3) d_->crop = float(q.cropFactorHint);
    else if (d_->cam) d_->crop = d_->cam->CropFactor;
    else d_->crop = d_->lens->CropFactor;
    d_->status = QStringLiteral("%1 @ %2 mm").arg(lensName()).arg(double(d_->focal), 0, 'f', 1);
    return true;
}

bool LensModel::hasProfile() const { return d_->lens != nullptr; }
bool LensModel::hasDistortion() const { return d_->lens && d_->lens->CalibDistortion && d_->lens->CalibDistortion[0]; }
bool LensModel::hasTCA() const { return d_->lens && d_->lens->CalibTCA && d_->lens->CalibTCA[0]; }
bool LensModel::hasVignetting() const { return d_->lens && d_->lens->CalibVignetting && d_->lens->CalibVignetting[0]; }
QString LensModel::cameraName() const {
    if (!d_->cam) return {};
    return QStringLiteral("%1 %2").arg(lf_mlstr_get(d_->cam->Maker), lf_mlstr_get(d_->cam->Model));
}
QString LensModel::lensName() const {
    if (!d_->lens) return {};
    return QStringLiteral("%1 %2").arg(lf_mlstr_get(d_->lens->Maker), lf_mlstr_get(d_->lens->Model));
}
QString LensModel::statusText() const { return d_->status; }

LensGrid LensModel::buildGrid(int W, int H, bool distortion, bool tca, int n) const {
    LensGrid g = LensGrid::identity(n);
    if (!d_->lens || W <= 0 || H <= 0 || (!distortion && !tca)) return g;
    int flags = 0;
    if (distortion && hasDistortion()) flags |= LF_MODIFY_DISTORTION;
    if (tca && hasTCA()) flags |= LF_MODIFY_TCA;
    if (!flags) return g;
    lfModifier mod(d_->lens, d_->crop, W, H);
    int got = mod.Initialize(d_->lens, LF_PF_F32, d_->focal, d_->aperture, d_->distance, 1.0f,
                             d_->lens->Type, flags, false);
    if (!got) return g;
    alignas(16) float res[6];
    for (int j = 0; j < n; ++j)
        for (int i = 0; i < n; ++i) {
            double u = -g.margin + (1.0 + 2.0 * g.margin) * i / (n - 1);
            double v = -g.margin + (1.0 + 2.0 * g.margin) * j / (n - 1);
            float x = float(u * W - 0.5), y = float(v * H - 0.5);
            if (!mod.ApplySubpixelGeometryDistortion(x, y, 1, 1, res)) continue;
            for (int ch = 0; ch < 3; ++ch) {
                size_t k = ((size_t(ch) * n + j) * n + i) * 2;
                g.uv[k] = float((res[ch * 2] + 0.5) / W);
                g.uv[k + 1] = float((res[ch * 2 + 1] + 0.5) / H);
            }
        }
    return g;
}

VignetteLut LensModel::buildVignette(int W, int H, int n) const {
    VignetteLut lut;
    if (!hasVignetting() || W <= 0 || H <= 0) return lut;
    lfModifier mod(d_->lens, d_->crop, W, H);
    int got = mod.Initialize(d_->lens, LF_PF_F32, d_->focal, d_->aperture, d_->distance, 1.0f,
                             d_->lens->Type, LF_MODIFY_VIGNETTING, false);
    if (!(got & LF_MODIFY_VIGNETTING)) return lut;
    lut.gain.resize(n);
    double cx = (W - 1) * 0.5, cy = (H - 1) * 0.5;
    for (int k = 0; k < n; ++k) {
        double t = double(k) / (n - 1);
        alignas(16) float px[4] = {1.f, 1.f, 1.f, 1.f};
        float x = float(cx + t * W * 0.5), y = float(cy + t * H * 0.5);
        if (!mod.ApplyColorModification(px, x, y, 1, 1, LF_CR_3(RED, GREEN, BLUE), 16)) { lut.gain.clear(); return lut; }
        lut.gain[k] = std::max(0.05f, px[1]);
    }
    return lut;
}

}  // namespace re
