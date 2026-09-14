#include "ui/MainWindow.h"
#include "core/OutputProfiles.h"
#include "io/RawLoader.h"
#include "ui/CanvasWidget.h"
#include "ui/ExportDialog.h"
#include "ui/FilmstripWidget.h"
#include "ui/SyncDialog.h"
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
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QElapsedTimer>
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
    filmstrip_ = new FilmstripWidget(session_, this);
    filmstrip_->hide();  // appears with the second image
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
    v->addWidget(filmstrip_);
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
        int idx = session_->indexOfPath(path);
        statusInfo_->setText(QStringLiteral("Decoding %1 (%2/%3) …").arg(QFileInfo(path).fileName()).arg(idx + 1).arg(session_->documentCount()));
    });
    connect(session_, &EditorSession::loadFinished, this, [this](bool ok, const QString& err) {
        if (ok) return;
        // A single bad file gets the dialog; in a set the filmstrip marks the one that failed.
        if (session_->documentCount() <= 1) QMessageBox::warning(this, QStringLiteral("Cannot open file"), err);
        else statusBar()->showMessage(QStringLiteral("Cannot open %1").arg(err), 8000);
        if (!session_->hasImage()) statusInfo_->setText(QStringLiteral("Failed to open file"));
    });
    connect(session_, &EditorSession::allLoadsFinished, this, [this] {
        busy_->hide();
        if (session_->hasImage()) statusInfo_->setText(session_->imageDescription());
    });
    connect(session_, &EditorSession::imageChanged, this, [this] {
        updateTitle();
        if (session_->hasImage()) statusInfo_->setText(session_->imageDescription());
        else if (!session_->isLoading()) statusInfo_->clear();
        statusLens_->setText(session_->lensStatus());
        updateDocumentActions();
    });
    connect(session_, &EditorSession::documentsChanged, this, [this] { updateTitle(); updateDocumentActions(); });
    connect(session_, &EditorSession::selectionChanged, this, [this] { updateTitle(); updateDocumentActions(); });
    connect(session_, &EditorSession::sidecarProblem, this, [this](const QString& m) { statusBar()->showMessage(m, 10000); });
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
    connect(basic_, &BasicPanel::whiteBalancePickerRequested, this, [this] { canvas_->setTool(canvas_->tool() == Tool::WhiteBalance ? Tool::Hand : Tool::WhiteBalance); });
    connect(basic_, &BasicPanel::autoToneRequested, this, &MainWindow::autoTone);
    connect(lens_, &LensPanel::autoCropRequested, canvas_, &CanvasWidget::autoCropToFit);
    connect(geometry_, &GeometryPanel::autoCropRequested, canvas_, &CanvasWidget::autoCropToFit);
    connect(geometry_, &GeometryPanel::toolRequested, this, [this](Tool t) { canvas_->setTool(canvas_->tool() == t ? Tool::Hand : t); });
    connect(canvas_, &CanvasWidget::toolChanged, geometry_, &GeometryPanel::setActiveTool);
    connect(geometry_, &GeometryPanel::cropAspectChanged, canvas_, &CanvasWidget::setCropAspect);

    qApp->installEventFilter(this);
    updateTitle();
    updateHistoryActions();
    updateDocumentActions();
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
    reset->setToolTip(QStringLiteral("Reset all adjustments of this image (R)"));
    syncBtn_ = new QPushButton(QStringLiteral("Sync…"), panel);
    syncBtn_->setToolTip(QStringLiteral("Copy this image's settings to all other loaded images (Ctrl+Shift+S)"));
    auto* exportBtn = new QPushButton(QStringLiteral("Export…"), panel);
    exportBtn->setToolTip(QStringLiteral("Export the selected images (Ctrl+E); File → Export all for the whole set"));
    row->addWidget(reset);
    row->addStretch();
    row->addWidget(syncBtn_);
    row->addWidget(exportBtn);
    v->addLayout(row);
    connect(reset, &QPushButton::clicked, this, [this] { session_->resetAll(); });
    connect(syncBtn_, &QPushButton::clicked, this, &MainWindow::syncSettings);
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
    auto* hint = new QLabel(QStringLiteral("Hold \\ for before/after · J clipping · X swaps crop orientation · PgUp/PgDn switch image · Ctrl/Shift-click selects several"), bar);
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
    file->addAction(QStringLiteral("&Open…"), QKeySequence::Open, this, &MainWindow::openDialog);
    exportAct_ = file->addAction(QStringLiteral("&Export…"), QKeySequence(Qt::CTRL | Qt::Key_E), this, &MainWindow::exportImage);
    exportAllAct_ = file->addAction(QStringLiteral("Export &all…"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_E), this, &MainWindow::exportAll);
    file->addSeparator();
    closeAct_ = file->addAction(QStringLiteral("&Close image"), QKeySequence(Qt::CTRL | Qt::Key_W), this, [this] { session_->closeDocument(session_->activeIndex()); });
    closeAllAct_ = file->addAction(QStringLiteral("Close all"), this, [this] { session_->closeAll(); });
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
    autoAct_ = edit->addAction(QStringLiteral("Auto &tone"), QKeySequence(Qt::CTRL | Qt::Key_U), this, &MainWindow::autoTone);
    edit->addSeparator();
    syncAct_ = edit->addAction(QStringLiteral("&Sync settings to all images…"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_S), this, &MainWindow::syncSettings);
    selectAllAct_ = edit->addAction(QStringLiteral("Select &all images"), QKeySequence::SelectAll, this, [this] { session_->selectAll(); });

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
    view->addSeparator();
    nextAct_ = view->addAction(QStringLiteral("&Next image"), this, [this] { stepImage(1); });
    nextAct_->setShortcuts({QKeySequence(Qt::Key_PageDown), QKeySequence(Qt::CTRL | Qt::Key_PageDown)});
    prevAct_ = view->addAction(QStringLiteral("&Previous image"), this, [this] { stepImage(-1); });
    prevAct_->setShortcuts({QKeySequence(Qt::Key_PageUp), QKeySequence(Qt::CTRL | Qt::Key_PageUp)});

    auto* tb = addToolBar(QStringLiteral("Tools"));
    tb->setMovable(false);
    tb->setToolButtonStyle(Qt::ToolButtonTextOnly);
    toolGroup_ = new QActionGroup(this);
    struct T { const char* label; const char* tip; Qt::Key key; Tool tool; };
    const T ts[] = {{"Hand", "Pan and zoom (H)", Qt::Key_H, Tool::Hand},
                    {"Crop", "Crop (C). Drag handles or draw a new rectangle; X swaps the aspect orientation", Qt::Key_C, Tool::Crop},
                    {"Straighten", "Drag a line along something that should be level (A)", Qt::Key_A, Tool::Straighten},
                    {"WB picker", "Click a neutral grey to set the white balance (W)", Qt::Key_W, Tool::WhiteBalance},
                    {"Perspective", "Drag the four corner handles (P)", Qt::Key_P, Tool::Perspective},
                    {"Guides", "Draw lines along things that should be vertical or horizontal (G); the perspective follows", Qt::Key_G, Tool::Guides}};
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
    auto* esc = new QAction(this);  // window-wide: works whichever panel widget has focus
    esc->setShortcut(QKeySequence(Qt::Key_Escape));
    esc->setShortcutContext(Qt::WindowShortcut);
    addAction(esc);
    connect(esc, &QAction::triggered, this, [this] { canvas_->setTool(Tool::Hand); });
    tb->addSeparator();
    tb->addAction(clipAct_);
    tb->addAction(beforeAct_);
    tb->addSeparator();
    tb->addAction(undoAct_);
    tb->addAction(redoAct_);
    tb->addSeparator();
    tb->addAction(syncAct_);
}

void MainWindow::setToolAction(Tool t) {
    int idx = int(t);
    if (idx >= 0 && idx < 6) toolActs_[idx]->setChecked(true);
}

void MainWindow::updateTitle() {
    QString name = session_->hasImage() ? QFileInfo(session_->filePath()).fileName() : QString();
    if (!name.isEmpty() && session_->documentCount() >= 2) {
        const int sel = session_->selectedCount();
        name += sel >= 2 ? QStringLiteral(" (%1/%2, %3 selected)").arg(session_->activeIndex() + 1).arg(session_->documentCount()).arg(sel)
                         : QStringLiteral(" (%1/%2)").arg(session_->activeIndex() + 1).arg(session_->documentCount());
    }
    setWindowTitle(name.isEmpty() ? QStringLiteral("photoshop") : QStringLiteral("%1 — photoshop").arg(name));
}

void MainWindow::updateHistoryActions() {
    undoAct_->setEnabled(session_->canUndo());
    redoAct_->setEnabled(session_->canRedo());
}

void MainWindow::updateDocumentActions() {
    const int n = session_->documentCount();
    const bool has = session_->hasImage();
    int selectedReady = 0;
    for (int i : session_->selectedIndices()) if (session_->document(i).ready()) ++selectedReady;
    exportAct_->setEnabled(has);
    exportAct_->setText(selectedReady >= 2 ? QStringLiteral("&Export %1 selected…").arg(selectedReady) : QStringLiteral("&Export…"));
    autoAct_->setEnabled(has);
    selectAllAct_->setEnabled(n >= 2);
    exportAllAct_->setEnabled(n >= 2 && session_->readyCount() >= 1);
    exportAllAct_->setText(n >= 2 ? QStringLiteral("Export &all (%1 images)…").arg(session_->readyCount()) : QStringLiteral("Export &all…"));
    syncAct_->setEnabled(has && n >= 2);
    syncBtn_->setEnabled(has && n >= 2);
    closeAct_->setEnabled(has);
    closeAllAct_->setEnabled(n > 0);
    nextAct_->setEnabled(n >= 2);
    prevAct_->setEnabled(n >= 2);
    filmstrip_->setVisible(n >= 2);
}

void MainWindow::stepImage(int direction) {
    const int n = session_->documentCount();
    if (n < 2) return;
    int from = session_->pendingActiveIndex() >= 0 ? session_->pendingActiveIndex() : session_->activeIndex();
    int i = from < 0 ? (direction > 0 ? -1 : 0) : from;
    for (int k = 0; k < n; ++k) {
        i = (i + direction + n) % n;
        if (session_->document(i).state != ImageDocument::State::Failed) { session_->setActiveIndex(i); return; }
    }
}

void MainWindow::openPaths(const QStringList& paths) {
    QStringList files;
    QStringList filters;
    for (const QString& e : RawLoader::supportedExtensions()) filters << "*." + e << "*." + e.toUpper();
    for (const QString& p : paths) {
        if (p.isEmpty()) continue;
        QFileInfo fi(p);
        if (fi.isDir()) {
            QDir dir(p);
            const QStringList names = dir.entryList(filters, QDir::Files | QDir::Readable, QDir::Name | QDir::IgnoreCase);
            if (names.isEmpty()) statusBar()->showMessage(QStringLiteral("No raw files in %1").arg(dir.dirName()), 5000);
            for (const QString& f : names) files << dir.filePath(f);
        } else {
            files << p;
        }
    }
    if (files.isEmpty()) return;
    session_->openFiles(files);
    QSettings().setValue("open/dir", QFileInfo(files.first()).absolutePath());
}

void MainWindow::openDialog() {
    QString dir = QSettings().value("open/dir").toString();
    QStringList paths = QFileDialog::getOpenFileNames(this, QStringLiteral("Open raw files"), dir, RawLoader::fileDialogFilter());
    openPaths(paths);
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

void MainWindow::autoTone() {
    if (!session_->hasImage() || !canvas_->backendReady()) return;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    canvas_->autoTone();
    QApplication::restoreOverrideCursor();
}

void MainWindow::syncSettings() {
    if (!session_->hasImage() || session_->documentCount() < 2) return;
    SyncDialog dlg(QFileInfo(session_->filePath()).fileName(), session_->documentCount() - 1, this);
    if (dlg.exec() != QDialog::Accepted) return;
    const SyncMask m = dlg.mask();
    int n = session_->syncToAll(m);
    if (n == 0) statusBar()->showMessage(QStringLiteral("Nothing to sync"), 4000);
    else statusBar()->showMessage(QStringLiteral("Synced %1 to %2 image%3").arg(SyncDialog::describe(m)).arg(n).arg(n == 1 ? QString() : QStringLiteral("s")), 6000);
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
    QStringList paths;
    for (const QUrl& u : e->mimeData()->urls())
        if (u.isLocalFile()) paths << u.toLocalFile();
    openPaths(paths);
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

std::unique_ptr<QProgressDialog> MainWindow::makeProgressDialog(int maximum) {
    auto prog = std::make_unique<QProgressDialog>(QStringLiteral("Rendering…"), QStringLiteral("Cancel"), 0, maximum, this);
    prog->setWindowTitle(QStringLiteral("Export"));
    prog->setWindowModality(Qt::WindowModal);
    prog->setMinimumDuration(0);
    prog->setAutoClose(false);
    prog->setAutoReset(false);
    prog->setValue(0);
    prog->show();
    return prog;
}

void MainWindow::exportImage() {
    if (!session_->hasImage() || !canvas_->backendReady()) return;
    std::vector<int> selected;
    for (int i : session_->selectedIndices()) if (session_->document(i).ready()) selected.push_back(i);
    if (selected.size() >= 2) { exportBatch(selected, QStringLiteral("selected images")); return; }
    ExportDialog dlg(session_, 0, this);
    if (dlg.exec() != QDialog::Accepted) return;
    const ExportSettings s = dlg.settings();
    const ImageDocument& doc = session_->document(session_->activeIndex());
    auto prog = makeProgressDialog(100);
    QString err, warn;
    bool ok = runExport(doc, s, ExportUi{prog.get(), 0, 100, {}}, &err, &warn);
    prog->close();
    if (!ok) {
        if (!err.isEmpty()) QMessageBox::warning(this, QStringLiteral("Export failed"), err);
        else statusBar()->showMessage(QStringLiteral("export cancelled"), 3000);
        return;
    }
    if (!warn.isEmpty()) QMessageBox::information(this, QStringLiteral("Exported with warnings"), warn);
    statusBar()->showMessage(QStringLiteral("Exported %1").arg(s.outputPath), 6000);
}

void MainWindow::exportAll() {
    std::vector<int> indices;
    for (int i = 0; i < session_->documentCount(); ++i)
        if (session_->document(i).ready()) indices.push_back(i);
    exportBatch(indices, QStringLiteral("images"));
}

void MainWindow::exportBatch(const std::vector<int>& indices, const QString& noun) {
    if (indices.empty() || !canvas_->backendReady()) return;
    ExportDialog dlg(session_, int(indices.size()), this, noun);
    if (dlg.exec() != QDialog::Accepted) return;
    const ExportSettings base = dlg.settings();
    const QDir dir(base.outputPath);
    const QString ext = Exporter::defaultExtension(base.format);
    auto targetFor = [&](const ImageDocument& d) { return dir.filePath(QFileInfo(d.path).completeBaseName() + "." + ext); };

    int existing = 0;
    for (int i : indices) if (QFileInfo::exists(targetFor(session_->document(i)))) ++existing;
    bool skipExisting = false;
    if (existing > 0) {
        QMessageBox box(QMessageBox::Question, QStringLiteral("Files exist"),
                        QStringLiteral("%1 of the %2 files already exist in %3.").arg(existing).arg(indices.size()).arg(dir.dirName()), QMessageBox::NoButton, this);
        auto* overwrite = box.addButton(QStringLiteral("Overwrite"), QMessageBox::AcceptRole);
        auto* skip = box.addButton(QStringLiteral("Skip existing"), QMessageBox::ActionRole);
        box.addButton(QMessageBox::Cancel);
        box.exec();
        if (box.clickedButton() == skip) skipExisting = true;
        else if (box.clickedButton() != overwrite) return;
    }

    const int n = int(indices.size());
    auto prog = makeProgressDialog(n * 100);
    int done = 0, skipped = 0;
    bool cancelled = false;
    QStringList problems;
    for (int k = 0; k < n && !cancelled; ++k) {
        const ImageDocument& doc = session_->document(indices[size_t(k)]);
        const QString target = targetFor(doc);
        if (skipExisting && QFileInfo::exists(target)) { ++skipped; continue; }
        ExportSettings s = base;
        s.outputPath = target;
        QString err, warn;
        ExportUi ui{prog.get(), k * 100, 100, QStringLiteral("%1/%2  %3 — ").arg(k + 1).arg(n).arg(doc.fileName())};
        if (runExport(doc, s, ui, &err, &warn)) {
            ++done;
            if (!warn.isEmpty()) problems << QStringLiteral("%1: %2").arg(doc.fileName(), warn);
        } else if (prog->wasCanceled()) {
            cancelled = true;
        } else {
            problems << QStringLiteral("%1: %2").arg(doc.fileName(), err.isEmpty() ? QStringLiteral("export failed") : err);
        }
    }
    prog->close();
    QString summary = QStringLiteral("Exported %1 of %2 images to %3").arg(done).arg(n).arg(base.outputPath);
    if (skipped) summary += QStringLiteral(", %1 skipped").arg(skipped);
    if (cancelled) summary += QStringLiteral(" (cancelled)");
    statusBar()->showMessage(summary, 8000);
    if (!problems.isEmpty()) QMessageBox::warning(this, QStringLiteral("Export finished with problems"), summary + QStringLiteral("\n\n") + problems.join('\n'));
}

bool MainWindow::runExport(const ImageDocument& doc, const ExportSettings& s, const ExportUi& ui, QString* error, QString* warning) {
    if (!doc.ready() || !doc.image) { if (error) *error = QStringLiteral("image not decoded"); return false; }
    const EditParams params = doc.params;
    auto report = [&](int pct, const QString& stage) {
        if (!ui.dialog) return true;
        ui.dialog->setLabelText(ui.prefix + stage + QStringLiteral("…"));
        ui.dialog->setValue(ui.base + pct * ui.span / 100);
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents | QEventLoop::AllEvents);
        return !ui.dialog->wasCanceled();
    };
    // No canvas repaints while the export owns the GPU: a repaint would swap the source texture under it.
    canvas_->setUpdatesEnabled(false);
    auto job = std::make_shared<ExportJob>();
    bool ok = canvas_->renderForExport(*doc.image, doc.grid, doc.vig, params, s.sampler, *job,
                                       [&](int pct, const QString& stage) { return report(pct * 40 / 100, stage); }, error);
    canvas_->setUpdatesEnabled(true);
    if (!ok) {
        if (ui.dialog && ui.dialog->wasCanceled() && error) error->clear();
        return false;
    }

    auto shared = std::make_shared<ExportShared>();
    const QString sourcePath = doc.path;
    QFutureWatcher<ExportResult> watcher;
    QEventLoop loop;
    connect(&watcher, &QFutureWatcher<ExportResult>::finished, &loop, &QEventLoop::quit);
    QTimer poll;
    poll.setInterval(50);
    connect(&poll, &QTimer::timeout, this, [&] {
        if (!ui.dialog) return;
        if (ui.dialog->wasCanceled()) shared->cancel = true;
        QString stage;
        { QMutexLocker l(&shared->mutex); stage = shared->stage; }
        report(40 + shared->pct.load() * 60 / 100, stage.isEmpty() ? QStringLiteral("Encoding") : stage);
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
    if (!r.ok) {
        if (error) *error = shared->cancel ? QString() : r.error;
        return false;
    }
    if (warning) *warning = r.warning;
    return true;
}

void MainWindow::runHeadless(const QString& screenshotPath, const QString& exportPath, ExportFormat format, bool disableLens, bool demo, bool sync,
                             bool reset, bool autoTone, const QString& guides, const QString& toolName, double zoom) {
    connect(session_, &EditorSession::sidecarProblem, this, [](const QString& m) { fprintf(stderr, "sidecar: %s\n", m.toUtf8().constData()); });
    connect(canvas_, &CanvasWidget::statusMessage, this, [](const QString& m) { fprintf(stdout, "status: %s\n", m.toUtf8().constData()); });
    connect(session_, &EditorSession::allLoadsFinished, this, [this, screenshotPath, exportPath, format, disableLens, demo, sync, reset, autoTone, guides, toolName, zoom] {
        if (!session_->hasImage()) {
            for (int i = 0; i < session_->documentCount(); ++i) fprintf(stderr, "load failed: %s: %s\n", session_->document(i).fileName().toUtf8().constData(), session_->document(i).error.toUtf8().constData());
            QCoreApplication::exit(2);
            return;
        }
        fprintf(stdout, "loaded: %s\nlens: %s\n", session_->imageDescription().toUtf8().constData(), session_->lensStatus().toUtf8().constData());
        if (session_->documentCount() > 1) {
            fprintf(stdout, "documents: %d (%d ready)\n", session_->documentCount(), session_->readyCount());
            for (int i = 0; i < session_->documentCount(); ++i) {
                const ImageDocument& d = session_->document(i);
                fprintf(stdout, "  %c %s%s%s\n", i == session_->activeIndex() ? '*' : ' ', d.fileName().toUtf8().constData(),
                        d.ready() ? "" : "  FAILED: ", d.ready() ? "" : d.error.toUtf8().constData());
            }
        }
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
            p.outputSharpenAmount = 60;
            p.geom.corners[0] = QVector2D(0.06f, 0.04f);
            p.geom.corners[1] = QVector2D(-0.04f, 0.05f);
            session_->setParams(p, false);
            canvas_->setTool(Tool::Crop);
            clipAct_->setChecked(true);
        }
        if (sync) {
            const SyncMask m = SyncMask();
            int n = session_->syncToAll(m);
            fprintf(stdout, "synced %s to %d image(s)\n", SyncDialog::describe(m).toUtf8().constData(), n);
        }
        if (reset) { session_->resetAll(); fprintf(stdout, "reset: %s\n", QFileInfo(session_->filePath()).fileName().toUtf8().constData()); }
        if (autoTone) {
            QElapsedTimer timer;
            timer.start();
            canvas_->autoTone();
            const ToneParams& t = session_->params().tone;
            fprintf(stdout, "auto tone (%lld ms): exposure %+.2f EV, contrast %+.0f, highlights %+.0f, shadows %+.0f, blacks %+.0f\n",
                    timer.elapsed(), t.exposureEV, t.contrast, t.highlights, t.shadows, t.blacks);
        }
        if (!guides.isEmpty()) {
            // "x1,y1,x2,y2;x1,y1,x2,y2" in frame coordinates of the current view (0..1)
            QList<QLineF> lines;
            for (const QString& seg : guides.split(';', Qt::SkipEmptyParts)) {
                const QStringList v = seg.split(',');
                if (v.size() == 4) lines << QLineF(v[0].toDouble(), v[1].toDouble(), v[2].toDouble(), v[3].toDouble());
            }
            canvas_->setTool(Tool::Guides);
            canvas_->addGuidesFromFrame(lines);
            const GeometryParams& g = session_->params().geom;
            fprintf(stdout, "guides: %d, corners", int(g.guides.size()));
            for (const QVector2D& c : g.corners) fprintf(stdout, " (%.4f, %.4f)", c.x(), c.y());
            fprintf(stdout, ", crop (%.4f, %.4f, %.4f, %.4f)\n", g.cropNorm.x(), g.cropNorm.y(), g.cropNorm.width(), g.cropNorm.height());
        }
        if (toolName == "perspective") canvas_->setTool(Tool::Perspective);
        else if (toolName == "guides") canvas_->setTool(Tool::Guides);
        else if (toolName == "crop") canvas_->setTool(Tool::Crop);
        else if (toolName == "straighten") canvas_->setTool(Tool::Straighten);
        else if (toolName == "hand") canvas_->setTool(Tool::Hand);
        if (zoom > 0) canvas_->setZoom(zoom);
        QTimer::singleShot(800, this, [this, screenshotPath, exportPath, format] {
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
                auto formatFromExt = [](const QString& ext) {
                    if (ext == "jpg" || ext == "jpeg") return ExportFormat::Jpeg;
                    if (ext == "png") return ExportFormat::Png16;
                    return ExportFormat::Tiff16;
                };
                if (QFileInfo(exportPath).isDir()) {
                    // Batch: every decoded image, named after its raw.
                    ExportSettings s;
                    s.format = format;
                    const QString ext = Exporter::defaultExtension(format);
                    for (int i = 0; i < session_->documentCount(); ++i) {
                        const ImageDocument& d = session_->document(i);
                        if (!d.ready()) continue;
                        s.outputPath = QDir(exportPath).filePath(QFileInfo(d.path).completeBaseName() + "." + ext);
                        QString err, warn;
                        if (!runExport(d, s, ExportUi{}, &err, &warn)) { fprintf(stderr, "export failed: %s: %s\n", d.fileName().toUtf8().constData(), err.toUtf8().constData()); rc = 4; }
                        else fprintf(stdout, "exported: %s%s%s\n", s.outputPath.toUtf8().constData(), warn.isEmpty() ? "" : " warning: ", warn.toUtf8().constData());
                    }
                } else {
                    ExportSettings s;
                    s.outputPath = exportPath;
                    s.format = formatFromExt(QFileInfo(exportPath).suffix().toLower());
                    QString err, warn;
                    if (!runExport(session_->document(session_->activeIndex()), s, ExportUi{}, &err, &warn)) { fprintf(stderr, "export failed: %s\n", err.toUtf8().constData()); rc = 4; }
                    else fprintf(stdout, "exported: %s%s%s\n", exportPath.toUtf8().constData(), warn.isEmpty() ? "" : " warning: ", warn.toUtf8().constData());
                }
            }
            QCoreApplication::exit(rc);
        });
    });
}

}  // namespace re
