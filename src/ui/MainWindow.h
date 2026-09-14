#pragma once
#include "io/Exporter.h"
#include "ui/EditorSession.h"
#include "ui/Tools.h"
#include <QMainWindow>
#include <memory>
#include <vector>

class QAction;
class QActionGroup;
class QLabel;
class QProgressBar;
class QProgressDialog;
class QPushButton;

namespace re {

class CanvasWidget;
class FilmstripWidget;
class BasicPanel;
class LensPanel;
class GeometryPanel;
class CurvePanel;
class DetailPanel;
class HistogramWidget;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    void openFile(const QString& path) { openPaths({path}); }
    void openPaths(const QStringList& paths);  // files and/or folders; a folder adds every raw in it
    // Headless helpers (offscreen testing): once every file is decoded, optionally apply the demo edits to
    // the active image, sync them to the others or reset the active image, grab the window to `screenshotPath`
    // and/or export with default settings to `exportPath` (a folder exports every loaded image in `format`), then quit.
    void runHeadless(const QString& screenshotPath, const QString& exportPath, ExportFormat format, bool disableLens, bool demo, bool sync,
                     bool reset, bool autoTone, const QString& guides, const QString& toolName, double zoom);

protected:
    bool eventFilter(QObject* obj, QEvent* ev) override;
    void dragEnterEvent(QDragEnterEvent* e) override;
    void dropEvent(QDropEvent* e) override;
    void closeEvent(QCloseEvent* e) override;

private:
    // Progress reporting for one export; several exports can share one dialog by each covering a range of it.
    struct ExportUi {
        QProgressDialog* dialog = nullptr;  // null = headless
        int base = 0, span = 100;
        QString prefix;                     // e.g. "2/8 DSC08912.ARW — "
    };
    void buildMenusAndToolbar();
    QWidget* buildRightPanel();
    QWidget* buildBottomBar();
    void openDialog();
    void exportImage();   // the selection: one image to a file, several to a folder
    void exportAll();
    void exportBatch(const std::vector<int>& indices, const QString& noun);
    void autoTone();
    void syncSettings();
    void stepImage(int direction);
    bool runExport(const ImageDocument& doc, const ExportSettings& s, const ExportUi& ui, QString* error, QString* warning);
    std::unique_ptr<QProgressDialog> makeProgressDialog(int maximum);
    void chooseDisplayProfile();
    void updateTitle();
    void updateHistoryActions();
    void updateDocumentActions();
    void setToolAction(Tool t);

    EditorSession* session_;
    CanvasWidget* canvas_;
    FilmstripWidget* filmstrip_;
    BasicPanel* basic_;
    LensPanel* lens_;
    GeometryPanel* geometry_;
    CurvePanel* curve_;
    DetailPanel* detail_;
    HistogramWidget* histogram_;
    QLabel* zoomLabel_;
    QLabel* statusInfo_;
    QLabel* statusLens_;
    QProgressBar* busy_;
    QPushButton* syncBtn_;
    QAction* undoAct_;
    QAction* redoAct_;
    QAction* exportAct_;
    QAction* exportAllAct_;
    QAction* syncAct_;
    QAction* autoAct_;
    QAction* selectAllAct_;
    QAction* closeAct_;
    QAction* closeAllAct_;
    QAction* nextAct_;
    QAction* prevAct_;
    QAction* clipAct_;
    QAction* beforeAct_;
    QActionGroup* toolGroup_;
    QAction* toolActs_[6];
};

}  // namespace re
