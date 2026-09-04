#pragma once
// Little-CMS wrappers: the working-space profile, standard output profiles,
// float transforms for export and a 3D LUT builder for ICC display previews.
#include <lcms2.h>
#include <QString>
#include <cstdint>
#include <memory>
#include <vector>

namespace re::icc {

enum class Space { SRGB = 0, AdobeRGB, DisplayP3, Rec2020Linear };
QString spaceName(Space s);
constexpr int kSpaceCount = 4;

struct ProfileDeleter { void operator()(void* h) const { if (h) cmsCloseProfile(h); } };
using Profile = std::unique_ptr<void, ProfileDeleter>;

Profile createWorkingProfile();          // linear Rec.2020 (D65 white, lcms adapts to the D50 PCS)
Profile createProfile(Space s);
Profile openProfile(const QString& path);
std::vector<uint8_t> profileBytes(cmsHPROFILE h);
QString profileDescription(cmsHPROFILE h);

// Working-linear float RGB -> encoded float RGB in the output profile, relative colorimetric.
class Transform {
public:
    explicit Transform(cmsHPROFILE output);
    ~Transform();
    bool valid() const { return xf_ != nullptr; }
    void apply(const float* rgbIn, float* rgbOut, size_t pixels) const;

private:
    Profile working_;
    cmsHTRANSFORM xf_ = nullptr;
};

// 3D LUT for the display path. Input axes are sRGB-OETF-encoded working values;
// output is display-encoded RGB. Returned as n*n*n*3 floats, r fastest.
std::vector<float> buildDisplayLut(cmsHPROFILE display, int n);

}  // namespace re::icc
