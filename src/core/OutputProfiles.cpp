#include "core/OutputProfiles.h"
#include "core/ColourMath.h"
#include <cmath>

namespace re::icc {

namespace {

void setDescription(cmsHPROFILE h, const char* desc) {
    cmsMLU* mlu = cmsMLUalloc(nullptr, 1);
    cmsMLUsetASCII(mlu, "en", "US", desc);
    cmsWriteTag(h, cmsSigProfileDescriptionTag, mlu);
    cmsMLUfree(mlu);
    cmsMLU* cp = cmsMLUalloc(nullptr, 1);
    cmsMLUsetASCII(cp, "en", "US", "No copyright, use freely");
    cmsWriteTag(h, cmsSigCopyrightTag, cp);
    cmsMLUfree(cp);
}

cmsCIExyY xyY(colour::Chromaticity c) { return cmsCIExyY{c.x, c.y, 1.0}; }
cmsCIExyYTRIPLE triple(const colour::Chromaticity p[3]) { return cmsCIExyYTRIPLE{xyY(p[0]), xyY(p[1]), xyY(p[2])}; }

Profile makeRGB(const colour::Chromaticity prim[3], cmsToneCurve* curve, const char* desc) {
    cmsCIExyY white = xyY(colour::kD65);
    cmsCIExyYTRIPLE prims = triple(prim);
    cmsToneCurve* curves[3] = {curve, curve, curve};
    cmsHPROFILE h = cmsCreateRGBProfile(&white, &prims, curves);
    cmsFreeToneCurve(curve);
    if (h) setDescription(h, desc);
    return Profile(h);
}

}  // namespace

QString spaceName(Space s) {
    switch (s) {
        case Space::SRGB: return QStringLiteral("sRGB");
        case Space::AdobeRGB: return QStringLiteral("Adobe RGB (1998)");
        case Space::DisplayP3: return QStringLiteral("Display P3");
        case Space::Rec2020Linear: return QStringLiteral("Rec.2020 linear (working space)");
    }
    return {};
}

Profile createWorkingProfile() {
    return makeRGB(colour::kRec2020, cmsBuildGamma(nullptr, 1.0), "Linear Rec.2020 (rawedit working space)");
}

Profile createProfile(Space s) {
    switch (s) {
        case Space::SRGB: {
            cmsHPROFILE h = cmsCreate_sRGBProfile();
            return Profile(h);
        }
        case Space::AdobeRGB:
            return makeRGB(colour::kAdobeRGB, cmsBuildGamma(nullptr, 2.19921875), "Adobe RGB (1998) compatible");
        case Space::DisplayP3: {
            cmsFloat64Number p[5] = {2.4, 1.0 / 1.055, 0.055 / 1.055, 1.0 / 12.92, 0.04045};
            return makeRGB(colour::kDisplayP3, cmsBuildParametricToneCurve(nullptr, 4, p), "Display P3");
        }
        case Space::Rec2020Linear:
            return createWorkingProfile();
    }
    return {};
}

Profile openProfile(const QString& path) {
    QByteArray p = path.toLocal8Bit();
    return Profile(cmsOpenProfileFromFile(p.constData(), "r"));
}

std::vector<uint8_t> profileBytes(cmsHPROFILE h) {
    std::vector<uint8_t> out;
    if (!h) return out;
    cmsUInt32Number n = 0;
    if (!cmsSaveProfileToMem(h, nullptr, &n) || n == 0) return out;
    out.resize(n);
    if (!cmsSaveProfileToMem(h, out.data(), &n)) out.clear();
    return out;
}

QString profileDescription(cmsHPROFILE h) {
    if (!h) return {};
    char buf[256] = {0};
    cmsUInt32Number n = cmsGetProfileInfoASCII(h, cmsInfoDescription, "en", "US", buf, sizeof(buf) - 1);
    return n ? QString::fromLatin1(buf) : QStringLiteral("(unnamed profile)");
}

Transform::Transform(cmsHPROFILE output) : working_(createWorkingProfile()) {
    if (!working_ || !output) return;
    xf_ = cmsCreateTransform(working_.get(), TYPE_RGB_FLT, output, TYPE_RGB_FLT, INTENT_RELATIVE_COLORIMETRIC,
                             cmsFLAGS_NOOPTIMIZE | cmsFLAGS_HIGHRESPRECALC);
}

Transform::~Transform() {
    if (xf_) cmsDeleteTransform(xf_);
}

void Transform::apply(const float* in, float* out, size_t pixels) const {
    if (!xf_) { for (size_t i = 0; i < pixels * 3; ++i) out[i] = in[i]; return; }
    const size_t chunk = 1 << 16;
    for (size_t p = 0; p < pixels; p += chunk) {
        size_t c = std::min(chunk, pixels - p);
        cmsDoTransform(xf_, in + p * 3, out + p * 3, cmsUInt32Number(c));
    }
}

std::vector<float> buildDisplayLut(cmsHPROFILE display, int n) {
    std::vector<float> lut(size_t(n) * n * n * 3, 0.f);
    Transform xf(display);
    auto eotf = [](double e) { return e <= 0.04045 ? e / 12.92 : std::pow((e + 0.055) / 1.055, 2.4); };
    std::vector<float> in(lut.size());
    size_t k = 0;
    for (int b = 0; b < n; ++b)
        for (int g = 0; g < n; ++g)
            for (int r = 0; r < n; ++r) {
                in[k++] = float(eotf(double(r) / (n - 1)));
                in[k++] = float(eotf(double(g) / (n - 1)));
                in[k++] = float(eotf(double(b) / (n - 1)));
            }
    xf.apply(in.data(), lut.data(), size_t(n) * n * n);
    for (float& v : lut) v = std::min(1.f, std::max(0.f, v));
    return lut;
}

}  // namespace re::icc
