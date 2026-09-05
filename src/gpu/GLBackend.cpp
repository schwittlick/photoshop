#include "gpu/GLBackend.h"
#include "core/CurveModel.h"
#include "core/Geometry.h"
#include <QOpenGLContext>
#include <QOpenGLVersionFunctionsFactory>
#include <QDebug>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace re {

namespace {
bool glDebugEnabled() {
    static const bool on = qEnvironmentVariableIsSet("RAWEDIT_GLDEBUG");
    return on;
}
}  // namespace

void GLBackend::checkGL(const char* where) {
    if (!glDebugEnabled()) return;
    GLenum e;
    while ((e = gl_->glGetError()) != GL_NO_ERROR) qWarning("GL error 0x%04x after %s", e, where);
}

GLBackend::GLBackend() = default;

GLBackend::~GLBackend() {
    if (gl_ && ctx_ && QOpenGLContext::currentContext() == ctx_) destroyAll();
}

void GLBackend::abandon() {
    gl_ = nullptr;
    ctx_ = nullptr;
    srcTex_ = gridTex_ = vigTex_ = curveTex_ = lutTex_ = vao_ = histSSBO_ = 0;
    for (auto& S : slots_) { S.t1 = S.t2 = S.t3 = Texture2D{}; S.has1 = S.has2 = S.has3 = false; }
}

void GLBackend::destroyAll() {
    clearSource();
    for (auto& S : slots_) { pool_->destroy(S.t1); pool_->destroy(S.t2); pool_->destroy(S.t3); pool_->destroy(S.t4tmp); pool_->destroy(S.t4); }
    GLuint texs[] = {gridTex_, vigTex_, curveTex_, lutTex_};
    for (GLuint t : texs) if (t) gl_->glDeleteTextures(1, &t);
    gridTex_ = vigTex_ = curveTex_ = lutTex_ = 0;
    if (vao_) gl_->glDeleteVertexArrays(1, &vao_);
    if (histSSBO_) gl_->glDeleteBuffers(1, &histSSBO_);
    vao_ = histSSBO_ = 0;
}

bool GLBackend::initialize(QString* error) {
    QOpenGLContext* ctx = QOpenGLContext::currentContext();
    if (!ctx) { if (error) *error = QStringLiteral("no current OpenGL context"); return false; }
    gl_ = QOpenGLVersionFunctionsFactory::get<QOpenGLFunctions_4_3_Core>(ctx);
    if (!gl_ || !gl_->initializeOpenGLFunctions()) {
        if (error) *error = QStringLiteral("OpenGL 4.3 is required (context is %1.%2)")
                                .arg(ctx->format().majorVersion()).arg(ctx->format().minorVersion());
        return false;
    }
    ctx_ = ctx;
    pool_ = std::make_unique<TexturePool>(gl_);
    QString err;
    if (!colour_.compileCompute(":/shaders/colour.comp", {}, &err)
        || !warp_.compileCompute(":/shaders/warp.comp", {}, &err)
        || !warpDebug_.compileCompute(":/shaders/warp.comp", {"DEBUG_COORDS 1"}, &err)
        || !tone8_.compileCompute(":/shaders/tone.comp", {}, &err)
        || !toneF_.compileCompute(":/shaders/tone.comp", {"FLOAT_OUTPUT 1"}, &err)
        || !sharpen1_.compileCompute(":/shaders/sharpen.comp", {}, &err)
        || !sharpen2_.compileCompute(":/shaders/sharpen.comp", {"PASS2 1"}, &err)
        || !present_.compileGraphics(":/shaders/present.vert", ":/shaders/present.frag", &err)) {
        if (error) *error = err;
        return false;
    }
    gl_->glGenVertexArrays(1, &vao_);
    gl_->glGenBuffers(1, &histSSBO_);
    gl_->glBindBuffer(GL_SHADER_STORAGE_BUFFER, histSSBO_);
    gl_->glBufferData(GL_SHADER_STORAGE_BUFFER, 1028 * sizeof(uint32_t), nullptr, GL_DYNAMIC_READ);
    gl_->glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

    setLensGrid(LensGrid::identity(2));
    gridValid_ = false;
    setVignetteLut(VignetteLut{});

    gl_->glGenTextures(1, &curveTex_);
    gl_->glBindTexture(GL_TEXTURE_1D, curveTex_);
    gl_->glTexStorage1D(GL_TEXTURE_1D, 1, GL_RGBA32F, curve::kLutSize);
    gl_->glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    gl_->glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    gl_->glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl_->glBindTexture(GL_TEXTURE_1D, 0);

    const char* renderer = reinterpret_cast<const char*>(gl_->glGetString(GL_RENDERER));
    const char* version = reinterpret_cast<const char*>(gl_->glGetString(GL_VERSION));
    info_ = QStringLiteral("%1 (%2)").arg(renderer ? renderer : "?", version ? version : "?");
    return true;
}

void GLBackend::setSource(const RawImage& img) {
    clearSource();
    if (img.width <= 0 || img.height <= 0 || img.rgb.size() < size_t(img.width) * img.height * 3) return;
    srcW_ = img.width;
    srcH_ = img.height;
    levels_ = 1 + int(std::floor(std::log2(double(std::max(srcW_, srcH_)))));
    gl_->glGenTextures(1, &srcTex_);
    gl_->glBindTexture(GL_TEXTURE_2D, srcTex_);
    gl_->glTexStorage2D(GL_TEXTURE_2D, levels_, GL_RGBA16F, srcW_, srcH_);
    gl_->glPixelStorei(GL_UNPACK_ALIGNMENT, 2);
    gl_->glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, srcW_, srcH_, GL_RGB, GL_UNSIGNED_SHORT, img.rgb.data());
    gl_->glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    gl_->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
    gl_->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    gl_->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl_->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    gl_->glGenerateMipmap(GL_TEXTURE_2D);
    gl_->glBindTexture(GL_TEXTURE_2D, 0);
    checkGL("source upload");

    invPreMul_ = Vec3(1.0 / std::max(img.preMul[0], 1e-4f), 1.0 / std::max(img.preMul[1], 1e-4f), 1.0 / std::max(img.preMul[2], 1e-4f));
    camera_ = img.camera;
    camToWork_ = img.camera.camToWorking();
    ++sourceVersion_;
}

void GLBackend::clearSource() {
    if (srcTex_) gl_->glDeleteTextures(1, &srcTex_);
    srcTex_ = 0;
    srcW_ = srcH_ = levels_ = 0;
    for (auto& S : slots_) S.has1 = S.has2 = S.has3 = S.has4 = false;
    ++sourceVersion_;
}

void GLBackend::setLensGrid(const LensGrid& grid) {
    if (!gridTex_) {
        gl_->glGenTextures(1, &gridTex_);
        gl_->glBindTexture(GL_TEXTURE_2D_ARRAY, gridTex_);
        gl_->glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        gl_->glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        gl_->glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        gl_->glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    LensGrid g = grid.valid() ? grid : LensGrid::identity(2);
    gl_->glBindTexture(GL_TEXTURE_2D_ARRAY, gridTex_);
    gl_->glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RG32F, g.n, g.n, 3, 0, GL_RG, GL_FLOAT, g.uv.data());
    gl_->glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
    gridN_ = g.n;
    gridMargin_ = float(g.margin);
    gridValid_ = grid.valid();
    ++gridVersion_;
}

void GLBackend::setVignetteLut(const VignetteLut& lut) {
    if (!vigTex_) {
        gl_->glGenTextures(1, &vigTex_);
        gl_->glBindTexture(GL_TEXTURE_1D, vigTex_);
        gl_->glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        gl_->glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        gl_->glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    }
    std::vector<float> data = lut.valid() ? lut.gain : std::vector<float>{1.f, 1.f};
    gl_->glBindTexture(GL_TEXTURE_1D, vigTex_);
    gl_->glTexImage1D(GL_TEXTURE_1D, 0, GL_R32F, int(data.size()), 0, GL_RED, GL_FLOAT, data.data());
    gl_->glBindTexture(GL_TEXTURE_1D, 0);
    vigValid_ = lut.valid();
    ++vigVersion_;
}

void GLBackend::setDisplayLut(const std::vector<float>& lut, int n) {
    if (lut.empty() || n < 2 || lut.size() < size_t(n) * n * n * 3) {
        lutN_ = 0;
        ++lutVersion_;
        return;
    }
    if (!lutTex_) {
        gl_->glGenTextures(1, &lutTex_);
        gl_->glBindTexture(GL_TEXTURE_3D, lutTex_);
        gl_->glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        gl_->glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        gl_->glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        gl_->glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        gl_->glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    }
    gl_->glBindTexture(GL_TEXTURE_3D, lutTex_);
    gl_->glTexImage3D(GL_TEXTURE_3D, 0, GL_RGB16F, n, n, n, 0, GL_RGB, GL_FLOAT, lut.data());
    gl_->glBindTexture(GL_TEXTURE_3D, 0);
    lutN_ = n;
    ++lutVersion_;
}

void GLBackend::ensureCurveLut(const ToneParams& t) {
    CurveKey k{t.pHighlights, t.pLights, t.pDarks, t.pShadows, t.curveMaster, t.curveR, t.curveG, t.curveB};
    if (curveKeyValid_ && k == curveKey_) return;
    curveKey_ = k;
    curveKeyValid_ = true;
    curveIdentity_ = curve::isIdentity(t);
    if (!curveIdentity_) {
        std::vector<float> rgb = curve::buildRgbLut(t, curve::kLutSize);
        std::vector<float> rgba(size_t(curve::kLutSize) * 4);
        for (int i = 0; i < curve::kLutSize; ++i) {
            rgba[i * 4 + 0] = rgb[i * 3 + 0];
            rgba[i * 4 + 1] = rgb[i * 3 + 1];
            rgba[i * 4 + 2] = rgb[i * 3 + 2];
            rgba[i * 4 + 3] = 1.f;
        }
        gl_->glBindTexture(GL_TEXTURE_1D, curveTex_);
        gl_->glTexSubImage1D(GL_TEXTURE_1D, 0, 0, curve::kLutSize, GL_RGBA, GL_FLOAT, rgba.data());
        gl_->glBindTexture(GL_TEXTURE_1D, 0);
    }
    ++curveVersion_;
}

void GLBackend::dispatch(int w, int h) {
    gl_->glDispatchCompute(GLuint((w + 15) / 16), GLuint((h + 15) / 16), 1);
}

TextureHandle GLBackend::render(int slot, const ViewSpec& view, const EditParams& p, const RenderOptions& optsIn,
                                HistogramData* hist) {
    if (!srcTex_ || slot < 0 || slot >= SlotCount || view.outWidth <= 0 || view.outHeight <= 0) return 0;
    RenderOptions opts = optsIn;
    if (opts.debugCoords) { opts.floatOutput = true; opts.histogram = false; }
    // Preview sharpening: a fourth pass on the encoded image, matching the export's unsharp mask.
    float sigma = p.outputSharpenRadius * opts.sharpenScale;
    bool sharpen = opts.sharpenScale > 0.f && p.outputSharpenAmount > 0.f && sigma >= 0.25f && !opts.debugCoords
                && !optsIn.floatOutput && opts.output != OutputMode::WorkingLinear;
    if (sharpen) opts.floatOutput = true;  // stage 3 writes floats so the mask sees unquantised values
    SlotCache& S = slots_[slot];
    int level = std::clamp(view.mipLevel, 0, std::max(0, levels_ - 1));
    int lw = std::max(1, srcW_ >> level), lh = std::max(1, srcH_ >> level);

    Stage1Key k1;
    k1.source = sourceVersion_;
    k1.vig = vigVersion_;
    k1.level = level;
    k1.wb = p.wb;
    k1.v = VigKey{p.lens.lensAuto, p.lens.profileVignetting, p.lens.vignetteAmount, p.lens.vignetteMidpoint};
    bool re1 = pool_->ensure(S.t1, lw, lh, GL_RGBA16F);
    if (re1 || !S.has1 || !(S.k1 == k1)) {
        runColour(S, level, p);
        S.k1 = k1; S.has1 = true; ++S.v1;
    }

    Stage2Key k2;
    k2.stage1 = S.v1;
    k2.grid = gridVersion_;
    k2.view = view;
    k2.geom = p.geom;
    k2.d = DistKey{p.lens.lensAuto, p.lens.profileCA, p.lens.profileDistortion, p.lens.distA, p.lens.distB, p.lens.distC,
                   p.lens.caRed, p.lens.caBlue};
    k2.sampler = opts.sampler;
    k2.debug = opts.debugCoords;
    bool re2 = pool_->ensure(S.t2, view.outWidth, view.outHeight, opts.debugCoords ? GL_RGBA32F : GL_RGBA16F);
    if (re2 || !S.has2 || !(S.k2 == k2)) {
        runWarp(S, view, p, opts);
        S.k2 = k2; S.has2 = true; ++S.v2;
    }

    ensureCurveLut(p.tone);
    Stage3Key k3;
    k3.stage2 = S.v2;
    k3.curve = curveVersion_;
    k3.lut = lutVersion_;
    k3.tone = p.tone;
    k3.output = opts.output;
    k3.overlay = opts.clipOverlay;
    k3.floatOut = opts.floatOutput;
    k3.passthrough = opts.debugCoords;
    k3.histogram = opts.histogram;
    bool re3 = pool_->ensure(S.t3, view.outWidth, view.outHeight, opts.floatOutput ? GL_RGBA32F : GL_RGBA8);
    if (re3 || !S.has3 || !(S.k3 == k3)) {
        runTone(S, p, opts);
        S.k3 = k3; S.has3 = true; ++S.v3;
    }
    if (hist) *hist = S.hist;
    if (!sharpen) return S.t3.id;

    Stage4Key k4{S.v3, sigma, p.outputSharpenAmount / 100.f * 1.5f};
    bool re4 = pool_->ensure(S.t4tmp, view.outWidth, view.outHeight, GL_RGBA16F);
    re4 |= pool_->ensure(S.t4, view.outWidth, view.outHeight, GL_RGBA8);
    if (re4 || !S.has4 || !(S.k4 == k4)) {
        runSharpen(S, k4.sigma, k4.strength);
        S.k4 = k4; S.has4 = true;
    }
    return S.t4.id;
}

void GLBackend::runColour(SlotCache& S, int level, const EditParams& p) {
    colour_.bind();
    gl_->glActiveTexture(GL_TEXTURE0);
    gl_->glBindTexture(GL_TEXTURE_2D, srcTex_);
    gl_->glActiveTexture(GL_TEXTURE1);
    gl_->glBindTexture(GL_TEXTURE_1D, vigTex_);
    gl_->glBindImageTexture(0, S.t1.id, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA16F);
    colour_.set("uLevel", level);
    colour_.setInt2("uSize", S.t1.w, S.t1.h);
    colour_.set("uInvPreMul", invPreMul_);
    colour_.set("uWbMul", camera_.multipliersFromTempTint(p.wb.temp, p.wb.tint));
    colour_.set("uCamToWork", camToWork_);
    colour_.set("uHighlightMode", 1);
    int vigMode = 0;
    if (p.lens.lensAuto && vigValid_ && p.lens.profileVignetting > 0) vigMode |= 1;
    if (p.lens.vignetteAmount != 0.f) vigMode |= 2;
    colour_.set("uVigMode", vigMode);
    colour_.set("uProfileVigStrength", p.lens.profileVignetting / 100.f);
    colour_.set("uVigAmount", p.lens.vignetteAmount / 100.f);
    colour_.set("uVigMidpoint", p.lens.vignetteMidpoint / 100.f);
    double aspect = double(srcW_) / double(srcH_);
    double hd = 0.5 * std::sqrt(aspect * aspect + 1.0);
    colour_.set("uHalfDiagScale", float(aspect / hd), float(1.0 / hd));
    dispatch(S.t1.w, S.t1.h);
    gl_->glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT | GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
    colour_.release();
    checkGL("colour stage");
}

void GLBackend::runWarp(SlotCache& S, const ViewSpec& view, const EditParams& p, const RenderOptions& o) {
    ShaderProgram& prog = o.debugCoords ? warpDebug_ : warp_;
    prog.bind();
    gl_->glActiveTexture(GL_TEXTURE0);
    gl_->glBindTexture(GL_TEXTURE_2D, S.t1.id);
    gl_->glActiveTexture(GL_TEXTURE1);
    gl_->glBindTexture(GL_TEXTURE_2D_ARRAY, gridTex_);
    gl_->glBindImageTexture(0, S.t2.id, 0, GL_FALSE, 0, GL_WRITE_ONLY, o.debugCoords ? GL_RGBA32F : GL_RGBA16F);

    geom::FrameGeometry fg = geom::FrameGeometry::compute(p.geom, srcW_, srcH_);
    prog.setInt2("uOutSize", S.t2.w, S.t2.h);
    prog.set("uRegionOrigin", float(view.region.x()), float(view.region.y()));
    prog.set("uRegionSize", float(view.region.width()), float(view.region.height()));
    prog.set("uCrop", float(view.crop.x()), float(view.crop.y()), float(view.crop.width()), float(view.crop.height()));
    prog.set("uAspect", float(fg.aspect));
    prog.set("uRot", float(fg.cosR), float(fg.sinR));
    prog.set("uInvPersp", fg.invPersp);
    bool useGrid = p.lens.lensAuto && gridValid_;
    prog.set("uUseGrid", useGrid ? 1 : 0);
    prog.set("uGridN", gridN_);
    prog.set("uGridMargin", gridMargin_);
    prog.set("uGridStrength", p.lens.profileDistortion / 100.f);
    prog.set("uGridCA", p.lens.profileCA ? 1 : 0);
    const float k = 0.25f / 100.f;
    prog.set("uManualDist", p.lens.distA * k, p.lens.distB * k, p.lens.distC * k);
    prog.set("uCaScale", 1.f + p.lens.caRed * 3e-5f, 1.f + p.lens.caBlue * 3e-5f);
    prog.set("uChannelsDiffer", (p.lens.caRed != 0.f || p.lens.caBlue != 0.f) ? 1 : 0);
    prog.set("uSampler", int(o.sampler));
    prog.set("uSrcSize", float(S.t1.w), float(S.t1.h));
    dispatch(S.t2.w, S.t2.h);
    gl_->glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT | GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
    prog.release();
    checkGL("warp stage");
}

void GLBackend::runTone(SlotCache& S, const EditParams& p, const RenderOptions& o) {
    ShaderProgram& prog = o.floatOutput ? toneF_ : tone8_;
    prog.bind();
    gl_->glActiveTexture(GL_TEXTURE0);
    gl_->glBindTexture(GL_TEXTURE_2D, S.t2.id);
    gl_->glActiveTexture(GL_TEXTURE2);
    gl_->glBindTexture(GL_TEXTURE_1D, curveTex_);
    if (lutTex_) {
        gl_->glActiveTexture(GL_TEXTURE3);
        gl_->glBindTexture(GL_TEXTURE_3D, lutTex_);
    }
    gl_->glBindImageTexture(0, S.t3.id, 0, GL_FALSE, 0, GL_WRITE_ONLY, o.floatOutput ? GL_RGBA32F : GL_RGBA8);

    bool wantHist = o.histogram && !o.debugCoords && o.output != OutputMode::WorkingLinear;
    if (wantHist) {
        static const std::vector<uint32_t> zeros(1028, 0u);
        gl_->glMemoryBarrier(GL_ALL_BARRIER_BITS);
        gl_->glBindBuffer(GL_SHADER_STORAGE_BUFFER, histSSBO_);
        gl_->glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, 1028 * sizeof(uint32_t), zeros.data());
        gl_->glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    }
    gl_->glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, histSSBO_);

    const ToneParams& t = p.tone;
    prog.setInt2("uSize", S.t3.w, S.t3.h);
    prog.set("uPassthrough", o.debugCoords ? 1 : 0);
    prog.set("uExposure", std::exp2(t.exposureEV));
    prog.set("uBlack", -t.blacks / 100.f * 0.02f);
    prog.set("uWhite", 1.f - t.whites / 100.f * 0.3f);
    prog.set("uHighlights", t.highlights / 100.f);
    prog.set("uShadows", t.shadows / 100.f);
    prog.set("uContrastK", 1.f + t.contrast / 100.f * 0.8f);
    prog.set("uSaturation", t.saturation / 100.f);
    prog.set("uVibrance", t.vibrance / 100.f);
    prog.set("uCurveEnabled", curveIdentity_ ? 0 : 1);
    prog.set("uCurveN", curve::kLutSize);
    int mode = 0;
    if (o.output == OutputMode::WorkingLinear) mode = 2;
    else if (o.output == OutputMode::DisplayLUT && lutN_ > 0) mode = 1;
    prog.set("uOutputMode", mode);
    prog.set("uLutN", std::max(lutN_, 2));
    prog.set("uWorkToSRGB", colour::workingToLinearSRGB());
    prog.set("uSRGBToWork", colour::linearSRGBToWorking());
    prog.set("uClipOverlay", o.clipOverlay ? 1 : 0);
    prog.set("uHistogram", wantHist ? 1 : 0);
    dispatch(S.t3.w, S.t3.h);
    gl_->glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT | GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_UPDATE_BARRIER_BIT
                         | GL_BUFFER_UPDATE_BARRIER_BIT);
    prog.release();
    checkGL("tone stage");

    if (wantHist) {
        std::vector<uint32_t> bins(1028, 0u);
        gl_->glBindBuffer(GL_SHADER_STORAGE_BUFFER, histSSBO_);
        gl_->glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, 1028 * sizeof(uint32_t), bins.data());
        gl_->glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
        for (int i = 0; i < 256; ++i) {
            S.hist.r[i] = bins[i];
            S.hist.g[i] = bins[256 + i];
            S.hist.b[i] = bins[512 + i];
            S.hist.l[i] = bins[768 + i];
        }
        S.hist.total = bins[1024];
        S.hist.clippedHigh = bins[1025];
        S.hist.clippedLow = bins[1026];
        S.hist.valid = true;
    } else {
        S.hist.valid = false;
    }
}

void GLBackend::releaseSlot(int slot) {
    if (slot < 0 || slot >= SlotCount || !gl_) return;
    SlotCache& S = slots_[slot];
    pool_->destroy(S.t1);
    pool_->destroy(S.t2);
    pool_->destroy(S.t3);
    S.has1 = S.has2 = S.has3 = false;
}

void GLBackend::runSharpen(SlotCache& S, float sigma, float strength) {
    gl_->glActiveTexture(GL_TEXTURE0);
    gl_->glBindTexture(GL_TEXTURE_2D, S.t3.id);
    gl_->glActiveTexture(GL_TEXTURE1);
    gl_->glBindTexture(GL_TEXTURE_2D, S.t4tmp.id);
    sharpen1_.bind();
    gl_->glBindImageTexture(0, S.t4tmp.id, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA16F);
    sharpen1_.setInt2("uSize", S.t4.w, S.t4.h);
    sharpen1_.set("uSigma", sigma);
    sharpen1_.set("uStrength", strength);
    dispatch(S.t4.w, S.t4.h);
    gl_->glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT | GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
    sharpen1_.release();
    sharpen2_.bind();
    gl_->glBindImageTexture(0, S.t4.id, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA8);
    sharpen2_.setInt2("uSize", S.t4.w, S.t4.h);
    sharpen2_.set("uSigma", sigma);
    sharpen2_.set("uStrength", strength);
    dispatch(S.t4.w, S.t4.h);
    gl_->glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT | GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_UPDATE_BARRIER_BIT);
    sharpen2_.release();
    checkGL("sharpen stage");
}

void GLBackend::present(TextureHandle tex, int fbWidth, int fbHeight, const QColor& bg) {
    if (!tex) return;
    gl_->glViewport(0, 0, fbWidth, fbHeight);
    gl_->glDisable(GL_DEPTH_TEST);
    gl_->glDisable(GL_BLEND);
    gl_->glDisable(GL_SCISSOR_TEST);
    present_.bind();
    gl_->glActiveTexture(GL_TEXTURE0);
    gl_->glBindTexture(GL_TEXTURE_2D, tex);
    present_.set("uBg", float(bg.redF()), float(bg.greenF()), float(bg.blueF()));
    gl_->glBindVertexArray(vao_);
    gl_->glDrawArrays(GL_TRIANGLES, 0, 3);
    gl_->glBindVertexArray(0);
    present_.release();
}

bool GLBackend::readback(TextureHandle tex, int w, int h, std::vector<float>& rgba) {
    if (!tex || w <= 0 || h <= 0) return false;
    gl_->glMemoryBarrier(GL_ALL_BARRIER_BITS);
    rgba.resize(size_t(w) * h * 4);
    while (gl_->glGetError() != GL_NO_ERROR) {}
    gl_->glBindTexture(GL_TEXTURE_2D, tex);
    gl_->glPixelStorei(GL_PACK_ALIGNMENT, 4);
    gl_->glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_FLOAT, rgba.data());
    gl_->glBindTexture(GL_TEXTURE_2D, 0);
    return gl_->glGetError() == GL_NO_ERROR;
}

}  // namespace re
