#include "ui/MainWindow.h"
#include "core/OutputProfiles.h"
#include "io/RawLoader.h"
#include "ui/CanvasWidget.h"
#include "ui/ExportDialog.h"
#include "ui/panels/BasicPanel.h"
#include "ui/panels/CurvePanel.h"
#include "ui/panels/DetailPanel.h"
#include "ui/panels/GeometryPanel.h"
#include "ui/panels/LensPanel.h"
#include "ui/widgets/HistogramWidget.h"
#include <QAbstractSpinBox>
#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QEventLoop>
#include <QFileDialog>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QMutex>
#include <QPainter>
#include <QProgressBar>
#include <QProgressDialog>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QStatusBar>
#include <QTabWidget>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrent>
#include <atomic>
#include <cstdio>
#include <memory>

namespace re {

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    session_ = new EditorSession(this);
    canvas_ = new CanvasWidget(session_, this);
    setAcceptDrops(true);

    auto* central = new QWidget(this);
    auto* h = new QHBoxLayout(central);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(0);
    auto* left = new QWidget(central);
    auto* v = new QVBoxLayout(left);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(0);
    v->addWidget(canvas_, 1);
    v->addWidget(buildBottomBar());
    h->addWidget(left, 1);
    h->addWidget(buildRightPanel());
    setCentralWidget(central);
    buildMenusAndToolbar();

    statusInfo_ = new QLabel(this);
    statusLens_ = new QLabel(this);
    busy_ = new QProgressBar(this);
    busy_->setRange(0, 0);
    busy_->setFixedWidth(120);
    busy_->hide();
    statusBar()->addWidget(statusInfo_, 1);
    statusBar()->addPermanentWidget(statusLens_);
    statusBar()->addPermanentWidget(busy_);

    connect(session_, &EditorSession::loadStarted, this, [this](const QString& path) {
        busy_->show();
        statusInfo_->setText(QStringLiteral("Decoding %1 …").arg(QFileInfo(path).fileName()));
        QApplication::setOverrideCursor(Qt::WaitCursor);
    });
    connect(session_, &EditorSession::loadFinished, this, [this](bool ok, const QString& err) {
        busy_->hide();
        QApplication::restoreOverrideCursor();
        if (!ok) {
            statusInfo_->setText(QStringLiteral("Failed to open file"));
            QMessageBox::warning(this, QStringLiteral("Cannot open file"), err);
        }
    });
    connect(session_, &EditorSession::imageChanged, this, [this] {
        updateTitle();
        statusInfo_->setText(session_->imageDescription());
        statusLens_->setText(session_->lensStatus());
        exportAct_->setEnabled(true);
    });
    connect(session_, &EditorSession::historyChanged, this, &MainWindow::updateHistoryActions);
    connect(canvas_, &CanvasWidget::histogramUpdated, histogram_, &HistogramWidget::setData);
    connect(canvas_, &CanvasWidget::histogramUpdated, curve_, &CurvePanel::setHistogram);
    connect(canvas_, &CanvasWidget::zoomChanged, this, [this](double z) { zoomLabel_->setText(QStringLiteral("%1%").arg(std::lround(z * 100))); });
    connect(canvas_, &CanvasWidget::statusMessage, this, [this](const QString& m) { statusBar()->showMessage(m, 4000); });
    connect(canvas_, &CanvasWidget::toolChanged, this, &MainWindow::setToolAction);
    connect(canvas_, &CanvasWidget::backendInitialised, this, [this](bool ok, const QString& info) {
        if (!ok) QMessageBox::critical(this, QStringLiteral("OpenGL"), QStringLiteral("Could not initialise the GPU pipeline:\n%1").arg(info));
        else statusInfo_->setToolTip(QStringLiteral("GPU: %1").arg(info));
    });
    connect(basic_, &BasicPanel::whiteBalancePickerRequested, this, [this] { canvas_->setTool(Tool::WhiteBalance); });
    connect(lens_, &LensPanel::autoCropRequested, canvas_, &CanvasWidget::autoCropToFit);
    connect(geometry_, &GeometryPanel::autoCropRequested, canvas_, &CanvasWidget::autoCropToFit);
    connect(geometry_, &GeometryPanel::toolRequested, canvas_, &CanvasWidget::setTool);
    connect(geometry_, &GeometryPanel::cropAspectChanged, canvas_, &CanvasWidget::setCropAspect);

    qApp->installEventFilter(this);
    updateTitle();
    updateHistoryActions();
    QSettings st;
    restoreGeometry(st.value("window/geometry").toByteArray());
}

void MainWindow::closeEvent(QCloseEvent* e) {
    QSettings st;
    st.setValue("window/geometry", saveGeometry());
    QMainWindow::closeEvent(e);
}

QWidget* MainWindow::buildRightPanel() {
    auto* panel = new QWidget(this);
    panel->setFixedWidth(360);
    auto* v = new QVBoxLayout(panel);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(4);
    auto* tabs = new QTabWidget(panel);
    tabs->setDocumentMode(true);
    auto wrap = [&](QWidget* w, const QString& title) {
        auto* sa = new QScrollArea(tabs);
        sa->setWidgetResizable(true);
        sa->setFrameShape(QFrame::NoFrame);
        sa->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        sa->setWidget(w);
        tabs->addTab(sa, title);
    };
    basic_ = new BasicPanel(session_);
    lens_ = new LensPanel(session_);
    geometry_ = new GeometryPanel(session_);
    curve_ = new CurvePanel(session_);
    detail_ = new DetailPanel(session_);
    wrap(basic_, QStringLiteral("Basic"));
    wrap(lens_, QStringLiteral("Lens"));
    wrap(geometry_, QStringLiteral("Geometry"));
    wrap(curve_, QStringLiteral("Curve"));
    wrap(detail_, QStringLiteral("Detail"));
    v->addWidget(tabs, 1);
    auto* row = new QHBoxLayout();
    row->setContentsMargins(6, 0, 6, 6);
    auto* reset = new QPushButton(QStringLiteral("Reset"), panel);
    reset->setToolTip(QStringLiteral("Reset all adjustments (R)"));
    auto* exportBtn = new QPushButton(QStringLiteral("Export…"), panel);
    exportBtn->setToolTip(QStringLiteral("Export (Ctrl+E)"));
    row->addWidget(reset);
    row->addStretch();
    row->addWidget(exportBtn);
    v->addLayout(row);
    connect(reset, &QPushButton::clicked, this, [this] { session_->resetAll(); });
    connect(exportBtn, &QPushButton::clicked, this, &MainWindow::exportImage);
    return panel;
}

QWidget* MainWindow::buildBottomBar() {
    auto* bar = new QWidget(this);
    bar->setFixedHeight(104);
    auto* h = new QHBoxLayout(bar);
    h->setContentsMargins(8, 4, 8, 4);
    h->setSpacing(6);
    auto* zoomBox = new QVBoxLayout();
    auto* zoomRow = new QHBoxLayout();
    struct Z { const char* label; double zoom; const char* key; };
    const Z zs[] = {{"Fit", 0, "0"}, {"25%", 0.25, "1"}, {"50%", 0.5, "2"}, {"100%", 1.0, "3"}, {"200%", 2.0, "4"}};
    for (const Z& z : zs) {
        auto* b = new QToolButton(bar);
        b->setText(QString::fromLatin1(z.label));
        b->setToolTip(QStringLiteral("Zoom %1 (%2)").arg(z.label, z.key));
        b->setAutoRaise(true);
        connect(b, &QToolButton::clicked, this, [this, z] { canvas_->setZoom(z.zoom); });
        zoomRow->addWidget(b);
    }
    zoomLabel_ = new QLabel(QStringLiteral("—"), bar);
    zoomLabel_->setMinimumWidth(48);
    zoomLabel_->setAlignment(Qt::AlignCenter);
    zoomRow->addWidget(zoomLabel_);
    zoomBox->addLayout(zoomRow);
    auto* hint = new QLabel(QStringLiteral("Hold \\ for before/after · J clipping · X swaps crop orientation"), bar);
    hint->setStyleSheet("color: palette(mid);");
    zoomBox->addWidget(hint);
    zoomBox->addStretch();
    h->addLayout(zoomBox);
    histogram_ = new HistogramWidget(bar);
    histogram_->setMaximumWidth(420);
    h->addWidget(histogram_, 1);
    return bar;
}

void MainWindow::buildMenusAndToolbar() {
    auto* file = menuBar()->addMenu(QStringLiteral("&File"));
    auto* open = file->addAction(QStringLiteral("&Open…"), QKeySequence::Open, this, &MainWindow::openDialog);
    Q_UNUSED(open);
    exportAct_ = file->addAction(QStringLiteral("&Export…"), QKeySequence(Qt::CTRL | Qt::Key_E), this, &MainWindow::exportImage);
    exportAct_->setEnabled(false);
    file->addSeparator();
    file->addAction(QStringLiteral("Display &profile (ICC)…"), this, &MainWindow::chooseDisplayProfile);
    file->addAction(QStringLiteral("Display as s&RGB"), this, [this] {
        canvas_->setDisplayLut({}, 0);
        statusBar()->showMessage(QStringLiteral("display: sRGB"), 3000);
    });
    file->addSeparator();
    file->addAction(QStringLiteral("&Quit"), QKeySequence::Quit, this, &QWidget::close);

    auto* edit = menuBar()->addMenu(QStringLiteral("&Edit"));
    undoAct_ = edit->addAction(QStringLiteral("&Undo"), QKeySequence::Undo, session_, &EditorSession::undo);
    redoAct_ = edit->addAction(QStringLiteral("&Redo"), this, [this] { session_->redo(); });
    redoAct_->setShortcuts({QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Z), QKeySequence(Qt::CTRL | Qt::Key_Y)});
    edit->addSeparator();
    edit->addAction(QStringLiteral("Reset &all adjustments"), QKeySequence(Qt::Key_R), session_, &EditorSession::resetAll);
    edit->addAction(QStringLiteral("White balance: as shot"), session_, &EditorSession::setWhiteBalanceAsShot);

    auto* view = menuBar()->addMenu(QStringLiteral("&View"));
    struct Z { const char* label; double zoom; Qt::Key key; };
    const Z zs[] = {{"Zoom to fit", 0, Qt::Key_0}, {"Zoom 25%", 0.25, Qt::Key_1}, {"Zoom 50%", 0.5, Qt::Key_2}, {"Zoom 100%", 1.0, Qt::Key_3}, {"Zoom 200%", 2.0, Qt::Key_4}};
    for (const Z& z : zs) view->addAction(QString::fromLatin1(z.label), QKeySequence(z.key), this, [this, z] { canvas_->setZoom(z.zoom); });
    view->addAction(QStringLiteral("Zoom in"), QKeySequence::ZoomIn, canvas_, &CanvasWidget::zoomIn);
    view->addAction(QStringLiteral("Zoom out"), QKeySequence::ZoomOut, canvas_, &CanvasWidget::zoomOut);
    view->addSeparator();
    clipAct_ = view->addAction(QStringLiteral("Show &clipping"), QKeySequence(Qt::Key_J));
    clipAct_->setCheckable(true);
    connect(clipAct_, &QAction::toggled, canvas_, &CanvasWidget::setShowClipping);
    beforeAct_ = view->addAction(QStringLiteral("&Before / after"), QKeySequence(Qt::Key_B));
    beforeAct_->setCheckable(true);
    beforeAct_->setToolTip(QStringLiteral("Toggle the unedited image (or hold \\)"));
    connect(beforeAct_, &QAction::toggled, canvas_, &CanvasWidget::setBeforeAfter);

    auto* tb = addToolBar(QStringLiteral("Tools"));
    tb->setMovable(false);
    tb->setToolButtonStyle(Qt::ToolButtonTextOnly);
    toolGroup_ = new QActionGroup(this);
    struct T { const char* label; const char* tip; Qt::Key key; Tool tool; };
    const T ts[] = {{"Hand", "Pan and zoom (H)", Qt::Key_H, Tool::Hand},
                    {"Crop", "Crop (C). Drag handles or draw a new rectangle; X swaps the aspect orientation", Qt::Key_C, Tool::Crop},
                    {"Straighten", "Drag a line along something that should be level (A)", Qt::Key_A, Tool::Straighten},
                    {"WB picker", "Click a neutral grey to set the white balance (W)", Qt::Key_W, Tool::WhiteBalance},
                    {"Perspective", "Drag the four corner handles (P)", Qt::Key_P, Tool::Perspective}};
    int i = 0;
    for (const T& t : ts) {
        auto* a = tb->addAction(QString::fromLatin1(t.label));
        a->setToolTip(QString::fromLatin1(t.tip));
        a->setCheckable(true);
        a->setShortcut(QKeySequence(t.key));
        toolGroup_->addAction(a);
        connect(a, &QAction::triggered, this, [this, t] { canvas_->setTool(t.tool); });
        toolActs_[i++] = a;
    }
    toolActs_[0]->setChecked(true);
    tb->addSeparator();
    tb->addAction(clipAct_);
    tb->addAction(beforeAct_);
    tb->addSeparator();
    tb->addAction(undoAct_);
    tb->addAction(redoAct_);
}

void MainWindow::setToolAction(Tool t) {
    int idx = int(t);
    if (idx >= 0 && idx < 5) toolActs_[idx]->setChecked(true);
}

void MainWindow::updateTitle() {
    QString name = session_->hasImage() ? QFileInfo(session_->filePath()).fileName() : QString();
    setWindowTitle(name.isEmpty() ? QStringLiteral("rawedit") : QStringLiteral("%1 — rawedit").arg(name));
}

void MainWindow::updateHistoryActions() {
    undoAct_->setEnabled(session_->canUndo());
    redoAct_->setEnabled(session_->canRedo());
}

void MainWindow::openFile(const QString& path) {
    if (path.isEmpty()) return;
    session_->openFile(path);
    QSettings().setValue("open/dir", QFileInfo(path).absolutePath());
}

void MainWindow::openDialog() {
    QString dir = QSettings().value("open/dir").toString();
    QString path = QFileDialog::getOpenFileName(this, QStringLiteral("Open raw file"), dir, RawLoader::fileDialogFilter());
    openFile(path);
}

void MainWindow::chooseDisplayProfile() {
    QString path = QFileDialog::getOpenFileName(this, QStringLiteral("Display ICC profile"), QStringLiteral("/usr/share/color/icc"),
                                                QStringLiteral("ICC profiles (*.icc *.icm *.ICC *.ICM)"));
    if (path.isEmpty()) return;
    icc::Profile prof = icc::openProfile(path);
    if (!prof) { QMessageBox::warning(this, QStringLiteral("ICC"), QStringLiteral("Could not read the profile.")); return; }
    const int n = 33;
    std::vector<float> lut = icc::buildDisplayLut(prof.get(), n);
    canvas_->setDisplayLut(lut, n);
    statusBar()->showMessage(QStringLiteral("display profile: %1").arg(icc::profileDescription(prof.get())), 5000);
}

bool MainWindow::eventFilter(QObject* obj, QEvent* ev) {
    if (ev->type() == QEvent::KeyPress || ev->type() == QEvent::KeyRelease) {
        auto* ke = static_cast<QKeyEvent*>(ev);
        if (ke->key() == Qt::Key_Backslash && !ke->isAutoRepeat()) {
            QWidget* fw = QApplication::focusWidget();
            if (!qobject_cast<QAbstractSpinBox*>(fw) && !qobject_cast<QLineEdit*>(fw)) {
                canvas_->setBeforeAfter(ev->type() == QEvent::KeyPress || beforeAct_->isChecked());
                return true;
            }
        }
    }
    return QMainWindow::eventFilter(obj, ev);
}

void MainWindow::dragEnterEvent(QDragEnterEvent* e) {
    if (e->mimeData()->hasUrls()) e->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent* e) {
    const auto urls = e->mimeData()->urls();
    if (!urls.isEmpty()) openFile(urls.first().toLocalFile());
}

// ------------------------------------------------------------------ export

namespace {
struct ExportShared {
    std::atomic<int> pct{0};
    std::atomic<bool> cancel{false};
    QMutex mutex;
    QString stage;
};
struct ExportResult {
    bool ok = false;
    QString error, warning;
};
}  // namespace

void MainWindow::exportImage() {
    if (!session_->hasImage() || !canvas_->backendReady()) return;
    ExportDialog dlg(session_, this);
    if (dlg.exec() != QDialog::Accepted) return;
    QString err, warn;
    const ExportSettings s = dlg.settings();
    if (!runExport(s, true, &err, &warn)) {
        if (!err.isEmpty()) QMessageBox::warning(this, QStringLiteral("Export failed"), err);
        else statusBar()->showMessage(QStringLiteral("export cancelled"), 3000);
        return;
    }
    if (!warn.isEmpty()) QMessageBox::information(this, QStringLiteral("Exported with warnings"), warn);
    statusBar()->showMessage(QStringLiteral("Exported %1").arg(s.outputPath), 6000);
}

bool MainWindow::runExport(const ExportSettings& s, bool interactive, QString* error, QString* warning) {
    const EditParams params = session_->params();
    std::unique_ptr<QProgressDialog> prog;
    if (interactive) {
        prog = std::make_unique<QProgressDialog>(QStringLiteral("Rendering…"), QStringLiteral("Cancel"), 0, 100, this);
        prog->setWindowTitle(QStringLiteral("Export"));
        prog->setWindowModality(Qt::WindowModal);
        prog->setMinimumDuration(0);
        prog->setAutoClose(false);
        prog->setAutoReset(false);
        prog->setValue(0);
        prog->show();
    }
    auto job = std::make_shared<ExportJob>();
    bool ok = canvas_->renderForExport(params, s.sampler, *job, [&](int pct, const QString& stage) {
        if (!prog) return true;
        prog->setLabelText(stage + QStringLiteral("…"));
        prog->setValue(pct * 40 / 100);
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents | QEventLoop::AllEvents);
        return !prog->wasCanceled();
    }, error);
    if (!ok) {
        if (prog && prog->wasCanceled() && error) error->clear();
        return false;
    }

    auto shared = std::make_shared<ExportShared>();
    const QString sourcePath = session_->filePath();
    QFutureWatcher<ExportResult> watcher;
    QEventLoop loop;
    connect(&watcher, &QFutureWatcher<ExportResult>::finished, &loop, &QEventLoop::quit);
    QTimer poll;
    poll.setInterval(50);
    connect(&poll, &QTimer::timeout, this, [&] {
        if (!prog) return;
        if (prog->wasCanceled()) shared->cancel = true;
        prog->setValue(40 + shared->pct.load() * 60 / 100);
        QMutexLocker l(&shared->mutex);
        if (!shared->stage.isEmpty()) prog->setLabelText(shared->stage + QStringLiteral("…"));
    });
    poll.start();
    watcher.setFuture(QtConcurrent::run([job, s, params, sourcePath, shared]() {
        ExportResult r;
        r.ok = Exporter::encode(*job, s, params, sourcePath, [shared](int pct, const QString& stage) {
            shared->pct = pct;
            QMutexLocker l(&shared->mutex);
            shared->stage = stage;
            return !shared->cancel.load();
        }, &r.error, &r.warning);
        return r;
    }));
    loop.exec();
    poll.stop();
    ExportResult r = watcher.result();
    if (prog) prog->close();
    if (!r.ok) {
        if (error) *error = shared->cancel ? QString() : r.error;
        return false;
    }
    if (warning) *warning = r.warning;
    return true;
}

void MainWindow::runHeadless(const QString& screenshotPath, const QString& exportPath, bool disableLens, bool demo) {
    connect(session_, &EditorSession::loadFinished, this, [this, screenshotPath, exportPath, disableLens, demo](bool ok, const QString& err) {
        if (!ok) { fprintf(stderr, "load failed: %s\n", err.toUtf8().constData()); QCoreApplication::exit(2); return; }
        fprintf(stdout, "loaded: %s\nlens: %s\n", session_->imageDescription().toUtf8().constData(), session_->lensStatus().toUtf8().constData());
        if (disableLens) { EditParams p = session_->params(); p.lens.lensAuto = false; session_->setParams(p, false); }
        if (demo) {
            // A representative edit across every stage, plus the crop tool for its overlay.
            EditParams p = session_->params();
            p.tone.exposureEV = 0.4f; p.tone.contrast = 25; p.tone.highlights = -30; p.tone.shadows = 20; p.tone.vibrance = 25;
            p.tone.curveMaster.pts = {QPointF(0, 0), QPointF(0.3, 0.26), QPointF(0.7, 0.75), QPointF(1, 1)};
            p.geom.rotationDeg = 2.5f;
            p.geom.perspVertical = 15;
            p.geom.cropNorm = QRectF(0.12, 0.08, 0.72, 0.8);
            session_->setParams(p, false);
            canvas_->setTool(Tool::Crop);
            clipAct_->setChecked(true);
        }
        QTimer::singleShot(800, this, [this, screenshotPath, exportPath] {
            int rc = 0;
            if (!screenshotPath.isEmpty()) {
                // QWidget::grab() does not include QOpenGLWidget content on every platform; render the GL
                // framebuffer first (which also feeds the histogram), then composite it into the window grab.
                QImage fb = canvas_->grabFramebuffer();
                QCoreApplication::processEvents();
                QPixmap pm = grab();
                {
                    QPainter painter(&pm);
                    QPoint origin = canvas_->mapTo(this, QPoint(0, 0));
                    painter.drawImage(QRect(origin, canvas_->size()), fb);
                }
                if (!pm.save(screenshotPath)) { fprintf(stderr, "screenshot failed\n"); rc = 3; }
                else fprintf(stdout, "screenshot: %s (%dx%d)\n", screenshotPath.toUtf8().constData(), pm.width(), pm.height());
            }
            if (!exportPath.isEmpty()) {
                ExportSettings s;
                s.outputPath = exportPath;
                QString ext = QFileInfo(exportPath).suffix().toLower();
                if (ext == "jpg" || ext == "jpeg") s.format = ExportFormat::Jpeg;
                else if (ext == "png") s.format = ExportFormat::Png16;
                else s.format = ExportFormat::Tiff16;
                QString err, warn;
                if (!runExport(s, false, &err, &warn)) { fprintf(stderr, "export failed: %s\n", err.toUtf8().constData()); rc = 4; }
                else fprintf(stdout, "exported: %s%s%s\n", exportPath.toUtf8().constData(), warn.isEmpty() ? "" : " warning: ", warn.toUtf8().constData());
            }
            QCoreApplication::exit(rc);
        });
    });
}

}  // namespace re
