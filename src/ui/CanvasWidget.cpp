#include "ui/CanvasWidget.h"
#include "core/Geometry.h"
#include "gpu/GLBackend.h"
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>

namespace re {

namespace {
const QColor kBackground(40, 40, 40);
constexpr double kFitMargin = 12.0;  // device px
}  // namespace

CanvasWidget::CanvasWidget(EditorSession* session, QWidget* parent) : QOpenGLWidget(parent), session_(session) {
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setMinimumSize(320, 240);
    connect(session_, &EditorSession::imageChanged, this, &CanvasWidget::onImageChanged);
    connect(session_, &EditorSession::paramsChanged, this, [this](const EditParams&, bool) { update(); });
}

CanvasWidget::~CanvasWidget() {
    if (backend_ && context()) {
        makeCurrent();
        backend_.reset();
        doneCurrent();
    }
}

// ------------------------------------------------------------------ GL

void CanvasWidget::initializeGL() {
    // Qt may call this again after a window change; the previous context (and its resources) is gone.
    if (backend_) static_cast<GLBackend*>(backend_.get())->abandon();
    backend_.reset();
    sourceUploaded_ = lensUploaded_ = false;
    connect(context(), &QOpenGLContext::aboutToBeDestroyed, this, [this] {
        if (!backend_) return;
        makeCurrent();
        backend_.reset();
        doneCurrent();
        backendOk_ = false;
        sourceUploaded_ = lensUploaded_ = false;
    }, Qt::DirectConnection);
    backend_ = std::make_unique<GLBackend>();
    QString err;
    backendOk_ = backend_->initialize(&err);
    if (!backendOk_) {
        emit backendInitialised(false, err);
        return;
    }
    emit backendInitialised(true, backend_->info());
    if (session_->hasImage()) onImageChanged();
}

void CanvasWidget::resizeGL(int, int) {}

void CanvasWidget::onImageChanged() {
    sourceUploaded_ = false;
    lensUploaded_ = false;
    zoom_ = 0;
    aspectSwapped_ = false;
    if (tool_ != Tool::Hand) setTool(Tool::Hand);
    update();
}

void CanvasWidget::uploadLensData() {
    backend_->setLensGrid(session_->lensGrid());
    backend_->setVignetteLut(session_->vignetteLut());
    lensUploaded_ = true;
}

void CanvasWidget::paintGL() {
    QPainter painter(this);
    painter.beginNativePainting();
    const double dpr = devicePixelRatioF();
    const int fbW = int(std::lround(width() * dpr)), fbH = int(std::lround(height() * dpr));
    auto* f = QOpenGLContext::currentContext()->functions();
    f->glViewport(0, 0, fbW, fbH);
    f->glClearColor(kBackground.redF(), kBackground.greenF(), kBackground.blueF(), 1.f);
    f->glClear(GL_COLOR_BUFFER_BIT);
    if (backendOk_ && session_->hasImage()) {
        if (!sourceUploaded_) {
            backend_->setSource(*session_->image());
            sourceUploaded_ = true;
        }
        if (!lensUploaded_) uploadLensData();
        const EditParams& p = beforeAfter_ ? session_->defaults() : session_->params();
        RenderOptions opts;
        opts.output = useLut_ ? OutputMode::DisplayLUT : OutputMode::DisplaySRGB;
        opts.clipOverlay = showClipping_ && !beforeAfter_;
        TextureHandle tex = backend_->render(RenderBackend::SlotCanvas, buildView(), p, opts);
        backend_->present(tex, fbW, fbH, kBackground);
        double z = effectiveZoom();
        if (std::abs(z - lastReportedZoom_) > 1e-9) { lastReportedZoom_ = z; emit zoomChanged(z); }
        // Whole-image histogram from a small proxy render.
        RenderOptions hopt;
        hopt.output = opts.output;
        hopt.histogram = true;
        HistogramData hist;
        backend_->render(RenderBackend::SlotHistogram, histogramView(), p, hopt, &hist);
        if (hist.valid) emit histogramUpdated(hist);
    }
    painter.endNativePainting();
    if (backendOk_ && session_->hasImage()) drawOverlays(painter);
    else if (!backendOk_) {
        painter.setPen(Qt::white);
        painter.drawText(rect(), Qt::AlignCenter, QStringLiteral("OpenGL 4.3 compute shaders are not available on this system."));
    } else if (!session_->isLoading()) {
        painter.setPen(QColor(150, 150, 150));
        painter.drawText(rect(), Qt::AlignCenter, QStringLiteral("Open a raw file  (Ctrl+O)"));
    }
}

// ------------------------------------------------------------------ view maths

QRectF CanvasWidget::viewCrop() const {
    if (tool_ == Tool::Crop || tool_ == Tool::Straighten || tool_ == Tool::Perspective) return QRectF(0, 0, 1, 1);
    return session_->params().geom.cropNorm;
}

QSizeF CanvasWidget::imagePixelSize() const {
    if (!session_->hasImage()) return {1, 1};
    auto img = session_->image();
    QRectF c = viewCrop();
    return {std::max(1.0, c.width() * img->width), std::max(1.0, c.height() * img->height)};
}

double CanvasWidget::fitZoom() const {
    const double dpr = devicePixelRatioF();
    QSizeF img = imagePixelSize();
    double zw = (width() * dpr - 2 * kFitMargin) / img.width();
    double zh = (height() * dpr - 2 * kFitMargin) / img.height();
    return std::max(1e-4, std::min(zw, zh));
}

double CanvasWidget::effectiveZoom() const { return zoom_ > 0 ? zoom_ : fitZoom(); }

QPointF CanvasWidget::effectivePan() const {
    if (zoom_ > 0) return pan_;
    const double dpr = devicePixelRatioF();
    QSizeF img = imagePixelSize();
    double z = fitZoom();
    return {(width() * dpr - z * img.width()) / 2.0, (height() * dpr - z * img.height()) / 2.0};
}

QPointF CanvasWidget::imageToScreen(QPointF imgPx) const {
    const double dpr = devicePixelRatioF();
    QPointF pan = effectivePan();
    double z = effectiveZoom();
    return {(pan.x() + z * imgPx.x()) / dpr, (pan.y() + z * imgPx.y()) / dpr};
}

QPointF CanvasWidget::screenToImage(QPointF s) const {
    const double dpr = devicePixelRatioF();
    QPointF pan = effectivePan();
    double z = effectiveZoom();
    return {(s.x() * dpr - pan.x()) / z, (s.y() * dpr - pan.y()) / z};
}

QPointF CanvasWidget::frameToScreen(Vec2 q) const {
    QRectF c = viewCrop();
    QSizeF img = imagePixelSize();
    return imageToScreen({(q.x - c.x()) / c.width() * img.width(), (q.y - c.y()) / c.height() * img.height()});
}

Vec2 CanvasWidget::screenToFrame(QPointF s) const {
    QRectF c = viewCrop();
    QSizeF img = imagePixelSize();
    QPointF ip = screenToImage(s);
    return {c.x() + ip.x() / img.width() * c.width(), c.y() + ip.y() / img.height() * c.height()};
}

ViewSpec CanvasWidget::buildView() const {
    const double dpr = devicePixelRatioF();
    ViewSpec v;
    v.outWidth = std::max(1, int(std::lround(width() * dpr)));
    v.outHeight = std::max(1, int(std::lround(height() * dpr)));
    QSizeF img = imagePixelSize();
    double z = effectiveZoom();
    QPointF pan = effectivePan();
    v.region = QRectF(-pan.x() / (z * img.width()), -pan.y() / (z * img.height()), v.outWidth / (z * img.width()), v.outHeight / (z * img.height()));
    v.crop = viewCrop();
    int levels = backend_ ? backend_->mipLevelCount() : 1;
    v.mipLevel = std::clamp(int(std::floor(std::log2(1.0 / z))), 0, std::max(0, levels - 1));
    return v;
}

ViewSpec CanvasWidget::histogramView() const {
    ViewSpec v;
    QRectF crop = session_->params().geom.cropNorm;
    auto img = session_->image();
    double w = crop.width() * img->width, h = crop.height() * img->height;
    double scale = std::min(1.0, 640.0 / std::max(w, h));
    v.outWidth = std::max(1, int(std::lround(w * scale)));
    v.outHeight = std::max(1, int(std::lround(h * scale)));
    v.region = QRectF(0, 0, 1, 1);
    v.crop = crop;
    int levels = backend_ ? backend_->mipLevelCount() : 1;
    v.mipLevel = std::clamp(int(std::floor(std::log2(1.0 / scale))), 0, std::max(0, levels - 1));
    return v;
}

void CanvasWidget::clampPan() {
    if (zoom_ <= 0) return;
    const double dpr = devicePixelRatioF();
    QSizeF img = imagePixelSize();
    double vw = width() * dpr, vh = height() * dpr;
    double iw = img.width() * zoom_, ih = img.height() * zoom_;
    if (iw <= vw) pan_.setX((vw - iw) / 2);
    else pan_.setX(std::clamp(pan_.x(), vw - iw, 0.0));
    if (ih <= vh) pan_.setY((vh - ih) / 2);
    else pan_.setY(std::clamp(pan_.y(), vh - ih, 0.0));
}

// ------------------------------------------------------------------ public slots

void CanvasWidget::setTool(Tool t) {
    if (tool_ == t) return;
    // Switching between crop-view (full frame) and normal view changes the image size; keep fit.
    bool wasFull = viewCrop() == QRectF(0, 0, 1, 1);
    tool_ = t;
    bool isFull = viewCrop() == QRectF(0, 0, 1, 1);
    if (wasFull != isFull) zoom_ = 0;
    drag_ = Drag::None;
    updateCursor(mapFromGlobal(QCursor::pos()));
    emit toolChanged(t);
    update();
}

void CanvasWidget::setCropAspect(CropAspect a) {
    cropAspect_ = a;
    aspectSwapped_ = false;
    if (session_->hasImage() && a != CropAspect::Free) {
        EditParams p = session_->params();
        QRectF r = constrainCrop(p.geom.cropNorm, 2);
        if (r != p.geom.cropNorm) { p.geom.cropNorm = r; session_->setParams(p, false); }
    }
    update();
}

void CanvasWidget::swapCropOrientation() {
    if (cropAspect_ == CropAspect::Free) return;
    aspectSwapped_ = !aspectSwapped_;
    EditParams p = session_->params();
    p.geom.cropNorm = constrainCrop(p.geom.cropNorm, 2);
    session_->setParams(p, false);
}

void CanvasWidget::setZoom(double z) {
    if (z <= 0) { zoom_ = 0; }
    else {
        // keep the viewport centre fixed
        const double dpr = devicePixelRatioF();
        QPointF centre(width() * dpr / 2, height() * dpr / 2);
        QPointF pan = effectivePan();
        double cur = effectiveZoom();
        QPointF imgPt = (centre - pan) / cur;
        zoom_ = std::clamp(z, 0.02, 16.0);
        pan_ = centre - imgPt * zoom_;
        clampPan();
    }
    emit zoomChanged(effectiveZoom());
    update();
}

void CanvasWidget::zoomIn() { setZoom(effectiveZoom() * 1.5); }
void CanvasWidget::zoomOut() { setZoom(effectiveZoom() / 1.5); }

void CanvasWidget::setShowClipping(bool on) { showClipping_ = on; update(); }
void CanvasWidget::setBeforeAfter(bool on) { if (beforeAfter_ != on) { beforeAfter_ = on; update(); } }

void CanvasWidget::setDisplayLut(const std::vector<float>& lut, int n) {
    if (!backendOk_) return;
    makeCurrent();
    backend_->setDisplayLut(lut, n);
    doneCurrent();
    useLut_ = !lut.empty();
    update();
}

bool CanvasWidget::renderForExport(const EditParams& params, Sampler sampler, ExportJob& job, const Exporter::Progress& progress, QString* error) {
    if (!backendOk_ || !session_->hasImage()) { if (error) *error = QStringLiteral("no image"); return false; }
    makeCurrent();
    if (!sourceUploaded_) { backend_->setSource(*session_->image()); sourceUploaded_ = true; }
    if (!lensUploaded_) uploadLensData();
    bool ok = Exporter::renderFullRes(*backend_, params, sampler, job, progress, error);
    backend_->releaseSlot(RenderBackend::SlotAux);
    doneCurrent();
    return ok;
}

void CanvasWidget::autoCropToFit(bool keepAspect) {
    if (!backendOk_ || !session_->hasImage()) return;
    auto img = session_->image();
    EditParams p = session_->params();
    const int mw = 640;
    const int mh = std::max(8, int(std::lround(mw * double(img->height) / img->width)));
    makeCurrent();
    if (!sourceUploaded_) { backend_->setSource(*img); sourceUploaded_ = true; }
    if (!lensUploaded_) uploadLensData();
    ViewSpec v;
    v.outWidth = mw;
    v.outHeight = mh;
    v.region = QRectF(0, 0, 1, 1);
    v.crop = QRectF(0, 0, 1, 1);
    int levels = backend_->mipLevelCount();
    v.mipLevel = std::clamp(int(std::floor(std::log2(double(img->width) / mw))), 0, std::max(0, levels - 1));
    RenderOptions o;
    o.debugCoords = true;
    std::vector<float> rgba;
    TextureHandle tex = backend_->render(RenderBackend::SlotAux, v, p, o);
    bool ok = tex && backend_->readback(tex, mw, mh, rgba);
    backend_->releaseSlot(RenderBackend::SlotAux);
    doneCurrent();
    if (!ok) { emit statusMessage(QStringLiteral("auto-crop: render failed")); return; }
    std::vector<uint8_t> mask(size_t(mw) * mh);
    for (size_t i = 0; i < mask.size(); ++i) mask[i] = rgba[i * 4 + 3] > 0.999f ? 1 : 0;
    double aspect = 0;
    if (keepAspect) {
        QRectF c = p.geom.cropNorm;
        aspect = (c.width() * img->width) / (c.height() * img->height) * (double(mh) / img->height) / (double(mw) / img->width);
    }
    QRect r = geom::largestInscribedRect(mask, mw, mh, aspect);
    if (r.isEmpty()) { emit statusMessage(QStringLiteral("auto-crop: no valid area found")); return; }
    // shrink by one mask pixel for safety
    QRectF rn((r.x() + 0.5) / mw, (r.y() + 0.5) / mh, (r.width() - 1.0) / mw, (r.height() - 1.0) / mh);
    p.geom.cropNorm = rn & QRectF(0, 0, 1, 1);
    session_->setParams(p, false);
    emit statusMessage(QStringLiteral("cropped to %1 × %2 px").arg(std::lround(rn.width() * img->width)).arg(std::lround(rn.height() * img->height)));
}

// ------------------------------------------------------------------ tools

int CanvasWidget::cropHandleAt(QPointF s) const {
    QRectF c = session_->params().geom.cropNorm;
    QPointF tl = frameToScreen({c.left(), c.top()}), br = frameToScreen({c.right(), c.bottom()});
    QRectF sr(tl, br);
    const double tol = 9.0;
    // 0 TL, 1 T, 2 TR, 3 R, 4 BR, 5 B, 6 BL, 7 L
    QPointF pts[8] = {sr.topLeft(), {sr.center().x(), sr.top()}, sr.topRight(), {sr.right(), sr.center().y()},
                      sr.bottomRight(), {sr.center().x(), sr.bottom()}, sr.bottomLeft(), {sr.left(), sr.center().y()}};
    for (int i = 0; i < 8; ++i)
        if (std::hypot(pts[i].x() - s.x(), pts[i].y() - s.y()) <= tol) return i;
    if (sr.contains(s)) return 8;
    return -1;
}

QRectF CanvasWidget::constrainCrop(QRectF r, int handle) const {
    if (!session_->hasImage()) return r;
    auto img = session_->image();
    double frameAspect = img->aspect();
    double A = cropAspectValue(cropAspect_, frameAspect, aspectSwapped_);
    // normalise
    if (r.width() < 0) { r.setLeft(r.left() + r.width()); r.setWidth(-r.width()); }
    if (r.height() < 0) { r.setTop(r.top() + r.height()); r.setHeight(-r.height()); }
    const double minSize = 0.02;
    r.setWidth(std::max(r.width(), minSize));
    r.setHeight(std::max(r.height(), minSize));
    if (A > 0) {
        // pixel aspect (w*W)/(h*H) = A  ->  h = w*W/(A*H)
        double hFromW = r.width() * img->width / (A * img->height);
        bool horizontalHandle = (handle == 3 || handle == 7);
        bool verticalHandle = (handle == 1 || handle == 5);
        if (verticalHandle) {
            double wFromH = r.height() * A * img->height / img->width;
            double cx = r.center().x();
            r.setLeft(cx - wFromH / 2); r.setWidth(wFromH);
        } else if (horizontalHandle) {
            double cy = r.center().y();
            r.setTop(cy - hFromW / 2); r.setHeight(hFromW);
        } else {
            // corner or whole: fit the aspect inside the dragged box, anchored at the handle's opposite corner
            double w = r.width(), h = r.height();
            if (hFromW <= h) h = hFromW; else w = h * A * img->height / img->width;
            bool anchorRight = (handle == 0 || handle == 6 || handle == 7);
            bool anchorBottom = (handle == 0 || handle == 1 || handle == 2);
            double x = anchorRight ? r.right() - w : r.left();
            double y = anchorBottom ? r.bottom() - h : r.top();
            r = QRectF(x, y, w, h);
        }
    }
    // keep inside the frame; shrink if needed while preserving aspect
    if (r.width() > 1 || r.height() > 1) {
        double s = std::min(1.0 / r.width(), 1.0 / r.height());
        r = QRectF(r.x(), r.y(), r.width() * s, r.height() * s);
    }
    if (r.left() < 0) r.moveLeft(0);
    if (r.top() < 0) r.moveTop(0);
    if (r.right() > 1) r.moveRight(1);
    if (r.bottom() > 1) r.moveBottom(1);
    return r;
}

void CanvasWidget::pickWhiteBalance(QPointF s) {
    if (!session_->hasImage()) return;
    auto img = session_->image();
    const EditParams& p = session_->params();
    // Trace the same inverse chain the shader uses, for the green channel.
    Vec2 q = screenToFrame(s);
    geom::FrameGeometry fg = geom::FrameGeometry::compute(p.geom, img->width, img->height);
    Vec2 lensQ = fg.frameToLens(q);
    const float k = 0.25f / 100.f;
    Vec2 src = geom::manualDistortion(lensQ, fg.aspect, p.lens.distA * k, p.lens.distB * k, p.lens.distC * k, 1.0);
    if (p.lens.lensAuto && session_->lensGrid().valid()) {
        Vec2 g = session_->lensGrid().sample(1, src);
        src = src + (g - src) * (p.lens.profileDistortion / 100.0);
    }
    if (src.x < 0 || src.y < 0 || src.x > 1 || src.y > 1) { emit statusMessage(QStringLiteral("eyedropper: outside the image")); return; }
    int px = std::clamp(int(src.x * img->width), 0, img->width - 1);
    int py = std::clamp(int(src.y * img->height), 0, img->height - 1);
    Vec3 raw = img->sampleRaw(px, py, 2);
    if (raw.maxComponent() > 0.98) { emit statusMessage(QStringLiteral("eyedropper: that area is clipped, pick a darker neutral")); return; }
    session_->setWhiteBalanceFromRaw(raw);
    emit statusMessage(QStringLiteral("white balance set from image"));
}

void CanvasWidget::applyStraighten() {
    QPointF d = lineEnd_ - lineStart_;
    if (std::hypot(d.x(), d.y()) < 8) return;
    double a = std::atan2(d.y(), d.x()) * 180.0 / kPi;  // screen angle, clockwise positive
    if (a > 45) a -= 90; else if (a < -45) a += 90;
    if (a > 45) a -= 90; else if (a < -45) a += 90;
    EditParams p = session_->params();
    p.geom.rotationDeg = float(std::clamp(double(p.geom.rotationDeg) - a, -45.0, 45.0));
    session_->setParams(p, false);
    emit statusMessage(QStringLiteral("rotated by %1°").arg(-a, 0, 'f', 2));
}

void CanvasWidget::updateCursor(QPointF s) {
    if (spaceHeld_ || drag_ == Drag::Pan) { setCursor(Qt::ClosedHandCursor); return; }
    switch (tool_) {
        case Tool::Hand: setCursor(zoom_ > 0 ? Qt::OpenHandCursor : Qt::ArrowCursor); break;
        case Tool::WhiteBalance: setCursor(Qt::CrossCursor); break;
        case Tool::Straighten: setCursor(Qt::CrossCursor); break;
        case Tool::Perspective: setCursor(Qt::ArrowCursor); break;
        case Tool::Crop: {
            int h = session_->hasImage() ? cropHandleAt(s) : -1;
            static const Qt::CursorShape shapes[8] = {Qt::SizeFDiagCursor, Qt::SizeVerCursor, Qt::SizeBDiagCursor, Qt::SizeHorCursor,
                                                      Qt::SizeFDiagCursor, Qt::SizeVerCursor, Qt::SizeBDiagCursor, Qt::SizeHorCursor};
            if (h >= 0 && h < 8) setCursor(shapes[h]);
            else if (h == 8) setCursor(Qt::SizeAllCursor);
            else setCursor(Qt::CrossCursor);
            break;
        }
    }
}

void CanvasWidget::mousePressEvent(QMouseEvent* e) {
    setFocus();
    if (!session_->hasImage()) return;
    QPointF s = e->position();
    dragStart_ = dragLast_ = s;
    if (e->button() == Qt::MiddleButton || (e->button() == Qt::LeftButton && (spaceHeld_ || tool_ == Tool::Hand))) {
        if (zoom_ <= 0) { zoom_ = fitZoom(); pan_ = effectivePan(); }
        drag_ = Drag::Pan;
        updateCursor(s);
        return;
    }
    if (e->button() != Qt::LeftButton) return;
    switch (tool_) {
        case Tool::WhiteBalance: pickWhiteBalance(s); break;
        case Tool::Straighten: lineStart_ = lineEnd_ = s; drag_ = Drag::Straighten; update(); break;
        case Tool::Crop: {
            int h = cropHandleAt(s);
            cropStart_ = session_->params().geom.cropNorm;
            if (h == 8) drag_ = Drag::CropMove;
            else if (h >= 0) { drag_ = Drag::CropHandle; cropHandle_ = h; }
            else drag_ = Drag::CropNew;
            update();
            break;
        }
        case Tool::Perspective: {
            const EditParams& p = session_->params();
            auto img = session_->image();
            geom::FrameGeometry fg = geom::FrameGeometry::compute(p.geom, img->width, img->height);
            auto quad = geom::perspectiveQuad(p.geom);
            perspHandle_ = -1;
            for (int i = 0; i < 4; ++i) {
                QPointF hs = frameToScreen(fg.rotateFwd(quad[i]));
                if (std::hypot(hs.x() - s.x(), hs.y() - s.y()) <= 10) perspHandle_ = i;
            }
            if (perspHandle_ >= 0) drag_ = Drag::Perspective;
            break;
        }
        default: break;
    }
}

void CanvasWidget::mouseMoveEvent(QMouseEvent* e) {
    QPointF s = e->position();
    if (drag_ == Drag::None) { updateCursor(s); return; }
    if (!session_->hasImage()) return;
    auto img = session_->image();
    switch (drag_) {
        case Drag::Pan: {
            const double dpr = devicePixelRatioF();
            pan_ += (s - dragLast_) * dpr;
            clampPan();
            update();
            break;
        }
        case Drag::Straighten: lineEnd_ = s; update(); break;
        case Drag::CropMove: {
            Vec2 a = screenToFrame(dragStart_), b = screenToFrame(s);
            QRectF r = cropStart_.translated(b.x - a.x, b.y - a.y);
            if (r.left() < 0) r.moveLeft(0);
            if (r.top() < 0) r.moveTop(0);
            if (r.right() > 1) r.moveRight(1);
            if (r.bottom() > 1) r.moveBottom(1);
            EditParams p = session_->params();
            p.geom.cropNorm = r;
            session_->setParams(p, true);
            break;
        }
        case Drag::CropHandle: {
            Vec2 f = screenToFrame(s);
            double fx = std::clamp(f.x, 0.0, 1.0), fy = std::clamp(f.y, 0.0, 1.0);
            QRectF r = cropStart_;
            switch (cropHandle_) {
                case 0: r.setTopLeft({fx, fy}); break;
                case 1: r.setTop(fy); break;
                case 2: r.setTopRight({fx, fy}); break;
                case 3: r.setRight(fx); break;
                case 4: r.setBottomRight({fx, fy}); break;
                case 5: r.setBottom(fy); break;
                case 6: r.setBottomLeft({fx, fy}); break;
                case 7: r.setLeft(fx); break;
            }
            EditParams p = session_->params();
            p.geom.cropNorm = constrainCrop(r, cropHandle_);
            session_->setParams(p, true);
            break;
        }
        case Drag::CropNew: {
            Vec2 a = screenToFrame(dragStart_), b = screenToFrame(s);
            QRectF r(QPointF(std::clamp(a.x, 0.0, 1.0), std::clamp(a.y, 0.0, 1.0)), QPointF(std::clamp(b.x, 0.0, 1.0), std::clamp(b.y, 0.0, 1.0)));
            int handle = (b.x >= a.x) ? (b.y >= a.y ? 4 : 2) : (b.y >= a.y ? 6 : 0);
            EditParams p = session_->params();
            p.geom.cropNorm = constrainCrop(r.normalized(), handle);
            session_->setParams(p, true);
            break;
        }
        case Drag::Perspective: {
            EditParams p = session_->params();
            geom::FrameGeometry fg = geom::FrameGeometry::compute(p.geom, img->width, img->height);
            Vec2 q = fg.rotateInv(screenToFrame(s));
            GeometryParams base = p.geom;
            base.corners = {};
            auto quad0 = geom::perspectiveQuad(base);  // unit square + keystone
            p.geom.corners[perspHandle_] = QVector2D(float(q.x - quad0[perspHandle_].x), float(q.y - quad0[perspHandle_].y));
            session_->setParams(p, true);
            break;
        }
        default: break;
    }
    dragLast_ = s;
}

void CanvasWidget::mouseReleaseEvent(QMouseEvent* e) {
    if (drag_ == Drag::None) return;
    Drag d = drag_;
    drag_ = Drag::None;
    switch (d) {
        case Drag::Straighten: lineEnd_ = e->position(); applyStraighten(); break;
        case Drag::CropMove: case Drag::CropHandle: case Drag::CropNew: case Drag::Perspective: session_->commit(); break;
        default: break;
    }
    updateCursor(e->position());
    update();
}

void CanvasWidget::mouseDoubleClickEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton && tool_ == Tool::Hand) setZoom(zoom_ > 0 ? 0 : 1.0);
}

void CanvasWidget::wheelEvent(QWheelEvent* e) {
    if (!session_->hasImage()) return;
    double steps = e->angleDelta().y() / 120.0;
    if (steps == 0) return;
    const double dpr = devicePixelRatioF();
    QPointF cursor = e->position() * dpr;
    double cur = effectiveZoom();
    QPointF pan = effectivePan();
    QPointF imgPt = (cursor - pan) / cur;
    zoom_ = std::clamp(cur * std::pow(1.25, steps), 0.02, 16.0);
    pan_ = cursor - imgPt * zoom_;
    clampPan();
    emit zoomChanged(zoom_);
    update();
    e->accept();
}

void CanvasWidget::keyPressEvent(QKeyEvent* e) {
    if (e->key() == Qt::Key_Escape && tool_ != Tool::Hand) { setTool(Tool::Hand); return; }
    if (e->key() == Qt::Key_X && tool_ == Tool::Crop) { swapCropOrientation(); return; }
    QOpenGLWidget::keyPressEvent(e);
}

// ------------------------------------------------------------------ overlays

void CanvasWidget::drawOverlays(QPainter& p) {
    p.setRenderHint(QPainter::Antialiasing);
    const EditParams& params = session_->params();
    auto img = session_->image();
    if (tool_ == Tool::Crop || tool_ == Tool::Straighten) {
        QRectF c = params.geom.cropNorm;
        QRectF sr(frameToScreen({c.left(), c.top()}), frameToScreen({c.right(), c.bottom()}));
        QRectF fr(frameToScreen({0, 0}), frameToScreen({1, 1}));
        QPainterPath dim;
        dim.addRect(fr.adjusted(-1, -1, 1, 1));
        dim.addRect(sr);
        p.fillPath(dim, QColor(0, 0, 0, 140));
        p.setPen(QPen(QColor(255, 255, 255, 220), 1));
        p.setBrush(Qt::NoBrush);
        p.drawRect(sr);
        if (drag_ == Drag::CropMove || drag_ == Drag::CropHandle || drag_ == Drag::CropNew) {
            p.setPen(QPen(QColor(255, 255, 255, 110), 1));
            for (int i = 1; i < 3; ++i) {
                p.drawLine(QPointF(sr.left() + sr.width() * i / 3, sr.top()), QPointF(sr.left() + sr.width() * i / 3, sr.bottom()));
                p.drawLine(QPointF(sr.left(), sr.top() + sr.height() * i / 3), QPointF(sr.right(), sr.top() + sr.height() * i / 3));
            }
        }
        if (tool_ == Tool::Crop) {
            p.setPen(Qt::NoPen);
            p.setBrush(Qt::white);
            QPointF pts[8] = {sr.topLeft(), {sr.center().x(), sr.top()}, sr.topRight(), {sr.right(), sr.center().y()},
                              sr.bottomRight(), {sr.center().x(), sr.bottom()}, sr.bottomLeft(), {sr.left(), sr.center().y()}};
            for (const QPointF& h : pts) p.drawRect(QRectF(h.x() - 4, h.y() - 4, 8, 8));
            p.setPen(QColor(255, 255, 255, 200));
            int w = int(std::lround(c.width() * img->width)), h = int(std::lround(c.height() * img->height));
            p.drawText(QPointF(sr.left() + 6, sr.top() - 6), QStringLiteral("%1 × %2").arg(w).arg(h));
        }
    }
    if (tool_ == Tool::Straighten && drag_ == Drag::Straighten) {
        p.setPen(QPen(QColor(255, 220, 80), 2));
        p.drawLine(lineStart_, lineEnd_);
        QPointF d = lineEnd_ - lineStart_;
        double a = std::atan2(d.y(), d.x()) * 180.0 / kPi;
        p.setPen(Qt::white);
        p.drawText(lineEnd_ + QPointF(10, -10), QStringLiteral("%1°").arg(a, 0, 'f', 1));
    }
    if (tool_ == Tool::Perspective) {
        geom::FrameGeometry fg = geom::FrameGeometry::compute(params.geom, img->width, img->height);
        auto quad = geom::perspectiveQuad(params.geom);
        QPolygonF poly;
        for (int i = 0; i < 4; ++i) poly << frameToScreen(fg.rotateFwd(quad[i]));
        p.setBrush(Qt::NoBrush);
        QRectF fr(frameToScreen({0, 0}), frameToScreen({1, 1}));
        p.setPen(QPen(QColor(255, 255, 255, 90), 1, Qt::DashLine));
        p.drawRect(fr);
        p.setPen(QPen(QColor(120, 200, 255, 220), 1.5));
        p.drawPolygon(poly);
        for (int i = 0; i < 4; ++i) {
            p.setPen(QPen(Qt::black, 1));
            p.setBrush(i == perspHandle_ && drag_ == Drag::Perspective ? QColor(255, 200, 60) : QColor(120, 200, 255));
            p.drawEllipse(poly[i], 6, 6);
        }
        p.setBrush(Qt::NoBrush);
    }
    if (beforeAfter_) {
        p.setPen(Qt::white);
        p.setBrush(QColor(0, 0, 0, 150));
        QRectF box(12, 12, 90, 24);
        p.drawRoundedRect(box, 4, 4);
        p.drawText(box, Qt::AlignCenter, QStringLiteral("BEFORE"));
    }
}

}  // namespace re
