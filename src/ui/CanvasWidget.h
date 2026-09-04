#pragma once
// The image canvas: owns the GL backend, renders the visible viewport region from the
// right proxy level, and hosts the interactive tools (hand, crop, straighten, WB
// eyedropper, perspective handles). Overlays are drawn with QPainter on top.
#include "gpu/RenderBackend.h"
#include "io/Exporter.h"
#include "ui/EditorSession.h"
#include "ui/Tools.h"
#include <QOpenGLWidget>
#include <QPointF>
#include <memory>

namespace re {

class CanvasWidget : public QOpenGLWidget {
    Q_OBJECT
public:
    explicit CanvasWidget(EditorSession* session, QWidget* parent = nullptr);
    ~CanvasWidget() override;

    RenderBackend* backend() const { return backend_.get(); }
    bool backendReady() const { return backendOk_; }
    Tool tool() const { return tool_; }
    CropAspect cropAspect() const { return cropAspect_; }
    double zoom() const { return zoom_; }  // 0 = fit
    double effectiveZoom() const;
    bool showClipping() const { return showClipping_; }

    // Full-resolution render for export; makes the context current itself.
    bool renderForExport(const EditParams& params, Sampler sampler, ExportJob& job, const Exporter::Progress& progress, QString* error);

public slots:
    void setTool(Tool t);
    void setCropAspect(CropAspect a);
    void swapCropOrientation();
    void setZoom(double z);  // <= 0 fits the image
    void zoomIn();
    void zoomOut();
    void setShowClipping(bool on);
    void setBeforeAfter(bool on);
    void autoCropToFit(bool keepAspect);
    void setDisplayLut(const std::vector<float>& lut, int n);  // empty -> plain sRGB

signals:
    void histogramUpdated(const HistogramData& hist);
    void zoomChanged(double zoom);
    void toolChanged(Tool t);
    void backendInitialised(bool ok, const QString& info);
    void statusMessage(const QString& msg);

protected:
    void initializeGL() override;
    void paintGL() override;
    void resizeGL(int w, int h) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    void keyPressEvent(QKeyEvent*) override;

private:
    enum class Drag { None, Pan, CropMove, CropHandle, CropNew, Straighten, Perspective };

    // geometry helpers
    QRectF viewCrop() const;                 // crop rect the canvas is showing (full frame in geometry tools)
    QSizeF imagePixelSize() const;           // size of viewCrop() in source pixels
    double fitZoom() const;
    QPointF effectivePan() const;            // device px
    QPointF imageToScreen(QPointF imgPx) const;  // logical px
    QPointF screenToImage(QPointF s) const;
    QPointF frameToScreen(Vec2 q) const;
    Vec2 screenToFrame(QPointF s) const;
    ViewSpec buildView() const;
    ViewSpec histogramView() const;
    void clampPan();
    void onImageChanged();
    void uploadLensData();
    void pickWhiteBalance(QPointF screenPos);
    void applyStraighten();
    int cropHandleAt(QPointF s) const;       // 0..7 handles, 8 inside, -1 none
    QRectF constrainCrop(QRectF r, int handle) const;
    void updateCursor(QPointF s);
    void drawOverlays(QPainter& p);

    EditorSession* session_;
    std::unique_ptr<RenderBackend> backend_;
    bool backendOk_ = false;
    bool sourceUploaded_ = false;
    bool lensUploaded_ = false;

    Tool tool_ = Tool::Hand;
    CropAspect cropAspect_ = CropAspect::Free;
    bool aspectSwapped_ = false;
    double zoom_ = 0;
    QPointF pan_{0, 0};
    bool showClipping_ = false;
    bool beforeAfter_ = false;
    bool useLut_ = false;

    Drag drag_ = Drag::None;
    QPointF dragStart_, dragLast_;
    QRectF cropStart_;
    int cropHandle_ = -1;
    int perspHandle_ = -1;
    QPointF lineStart_, lineEnd_;
    bool spaceHeld_ = false;
    double lastReportedZoom_ = -1;
};

}  // namespace re
