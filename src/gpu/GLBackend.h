#pragma once
#include "gpu/RenderBackend.h"
#include "gpu/ShaderProgram.h"
#include "gpu/TexturePool.h"
#include <QOpenGLFunctions_4_3_Core>
#include <memory>

namespace re {

// OpenGL 4.3 core implementation of RenderBackend built on compute shaders.
// Requires a current context for every call.
class GLBackend : public RenderBackend {
public:
    GLBackend();
    ~GLBackend() override;

    bool initialize(QString* error) override;
    // Call when the GL context was lost/destroyed: forget all GL handles without touching GL.
    void abandon();
    QString info() const override { return info_; }

    void setSource(const RawImage& img) override;
    void clearSource() override;
    bool hasSource() const override { return srcTex_ != 0; }
    int sourceWidth() const override { return srcW_; }
    int sourceHeight() const override { return srcH_; }
    int mipLevelCount() const override { return levels_; }

    void setLensGrid(const LensGrid& grid) override;
    void setVignetteLut(const VignetteLut& lut) override;
    void setDisplayLut(const std::vector<float>& lut, int n) override;

    TextureHandle render(int slot, const ViewSpec& view, const EditParams& params, const RenderOptions& opts,
                         HistogramData* hist = nullptr) override;
    void releaseSlot(int slot) override;
    void present(TextureHandle tex, int fbWidth, int fbHeight, const QColor& bg) override;
    bool readback(TextureHandle tex, int w, int h, std::vector<float>& rgba) override;

private:
    struct VigKey {
        bool lensAuto = true;
        float profileVignetting = 0, amount = 0, midpoint = 0;
        bool operator==(const VigKey&) const = default;
    };
    struct DistKey {
        bool lensAuto = true, profileCA = true;
        float profileDistortion = 0, a = 0, b = 0, c = 0, caR = 0, caB = 0;
        bool operator==(const DistKey&) const = default;
    };
    struct Stage1Key {
        unsigned source = 0, vig = 0;
        int level = -1;
        WhiteBalance wb;
        VigKey v;
        bool operator==(const Stage1Key&) const = default;
    };
    struct Stage2Key {
        unsigned stage1 = 0, grid = 0;
        ViewSpec view;
        GeometryParams geom;
        DistKey d;
        Sampler sampler = Sampler::CatmullRom;
        bool debug = false;
        bool operator==(const Stage2Key&) const = default;
    };
    struct Stage3Key {
        unsigned stage2 = 0, curve = 0, lut = 0;
        ToneParams tone;
        OutputMode output = OutputMode::DisplaySRGB;
        bool overlay = false, floatOut = false, passthrough = false, histogram = false;
        bool operator==(const Stage3Key&) const = default;
    };
    struct SlotCache {
        Texture2D t1, t2, t3;
        Stage1Key k1;
        Stage2Key k2;
        Stage3Key k3;
        bool has1 = false, has2 = false, has3 = false;
        unsigned v1 = 0, v2 = 0;
        HistogramData hist;
    };
    struct CurveKey {
        float h = 0, l = 0, d = 0, s = 0;
        CurvePoints m, r, g, b;
        bool operator==(const CurveKey&) const = default;
    };

    void runColour(SlotCache& S, int level, const EditParams& p);
    void runWarp(SlotCache& S, const ViewSpec& view, const EditParams& p, const RenderOptions& o);
    void runTone(SlotCache& S, const EditParams& p, const RenderOptions& o);
    void ensureCurveLut(const ToneParams& t);
    void dispatch(int w, int h);
    void checkGL(const char* where);
    void destroyAll();

    QOpenGLFunctions_4_3_Core* gl_ = nullptr;
    QOpenGLContext* ctx_ = nullptr;
    std::unique_ptr<TexturePool> pool_;
    ShaderProgram colour_, warp_, warpDebug_, tone8_, toneF_, present_;
    QString info_;
    GLuint vao_ = 0, histSSBO_ = 0;

    GLuint srcTex_ = 0;
    int srcW_ = 0, srcH_ = 0, levels_ = 0;
    unsigned sourceVersion_ = 0;
    Vec3 invPreMul_{1, 1, 1};
    Mat3 camToWork_;
    colour::CameraColour camera_;

    GLuint gridTex_ = 0;
    int gridN_ = 0;
    float gridMargin_ = 0.25f;
    bool gridValid_ = false;
    unsigned gridVersion_ = 0;
    GLuint vigTex_ = 0;
    bool vigValid_ = false;
    unsigned vigVersion_ = 0;

    GLuint curveTex_ = 0;
    unsigned curveVersion_ = 0;
    bool curveIdentity_ = true;
    bool curveKeyValid_ = false;
    CurveKey curveKey_;

    GLuint lutTex_ = 0;
    int lutN_ = 0;
    unsigned lutVersion_ = 0;

    SlotCache slots_[SlotCount];
};

}  // namespace re
