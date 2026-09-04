#pragma once
// Abstract GPU backend. The whole edit chain runs as three cached stages per slot:
//   1 colour : source mip -> white balance, highlight clamp, camera->working, vignetting   (source frame)
//   2 warp   : distortion + CA + rotation + perspective + crop in one resample              (output frame)
//   3 tone   : exposure .. curve .. saturation, output transform, histogram, overlays
// The tone stage sits after the warp so that tone edits never re-run the warp and so that
// resampling happens in linear light. Render output is a pure function of its inputs.
#include "core/EditParams.h"
#include "core/LensModel.h"
#include "io/RawImage.h"
#include <QColor>
#include <QRectF>
#include <QString>
#include <array>
#include <cstdint>
#include <vector>

namespace re {

struct ViewSpec {
    int outWidth = 0, outHeight = 0;  // output pixels
    QRectF region{0, 0, 1, 1};        // portion of `crop` (0..1) covered by the output; may extend outside
    QRectF crop{0, 0, 1, 1};          // frame-normalised rectangle that maps onto region (0..1)
    int mipLevel = 0;                 // source mip level feeding the colour stage
    bool operator==(const ViewSpec&) const = default;
};

enum class OutputMode { DisplaySRGB = 0, DisplayLUT = 1, WorkingLinear = 2 };
enum class Sampler { Bilinear = 0, CatmullRom = 1, Lanczos3 = 2 };

struct RenderOptions {
    OutputMode output = OutputMode::DisplaySRGB;
    Sampler sampler = Sampler::CatmullRom;
    bool clipOverlay = false;
    bool histogram = false;
    bool floatOutput = false;   // final texture RGBA32F instead of RGBA8
    bool debugCoords = false;   // warp writes (u, v, 0, coverage) in source coords; tone passes through
    bool operator==(const RenderOptions&) const = default;
};

struct HistogramData {
    std::array<uint32_t, 256> r{}, g{}, b{}, l{};
    uint32_t total = 0, clippedHigh = 0, clippedLow = 0;
    bool valid = false;
};

using TextureHandle = unsigned;

class RenderBackend {
public:
    enum Slot { SlotCanvas = 0, SlotHistogram = 1, SlotAux = 2, SlotCount = 3 };

    virtual ~RenderBackend() = default;
    virtual bool initialize(QString* error) = 0;
    virtual QString info() const = 0;

    virtual void setSource(const RawImage& img) = 0;
    virtual void clearSource() = 0;
    virtual bool hasSource() const = 0;
    virtual int sourceWidth() const = 0;
    virtual int sourceHeight() const = 0;
    virtual int mipLevelCount() const = 0;

    virtual void setLensGrid(const LensGrid& grid) = 0;
    virtual void setVignetteLut(const VignetteLut& lut) = 0;
    // n^3*3 floats (r fastest), indexed by sRGB-encoded working values. Empty disables the LUT path.
    virtual void setDisplayLut(const std::vector<float>& lut, int n) = 0;

    virtual TextureHandle render(int slot, const ViewSpec& view, const EditParams& params,
                                 const RenderOptions& opts, HistogramData* hist = nullptr) = 0;
    // Free the cached stage textures of a slot (e.g. after an export rendered at full resolution).
    virtual void releaseSlot(int slot) = 0;
    // Draw a rendered texture 1:1 into the bound framebuffer, composited over bg.
    virtual void present(TextureHandle tex, int fbWidth, int fbHeight, const QColor& bg) = 0;
    // Read a rendered texture back as RGBA32F (w*h*4 floats).
    virtual bool readback(TextureHandle tex, int w, int h, std::vector<float>& rgba) = 0;
};

}  // namespace re
