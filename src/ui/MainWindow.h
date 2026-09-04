#pragma once
#include "ui/EditorSession.h"
#include "ui/Tools.h"
#include <QMainWindow>
#include "io/Exporter.h"

class QAction;
class QActionGroup;
class QLabel;
class QProgressBar;

namespace re {

class CanvasWidget;
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
    void openFile(const QString& path);
    // Headless helpers (offscreen testing): after the next successful load, grab the window
    // to `screenshotPath` and/or export with default settings to `exportPath`, then quit.
    void runHeadless(const QString& screenshotPath, const QString& exportPath, bool disableLens, bool demo, const QString& toolName);

protected:
    bool eventFilter(QObject* obj, QEvent* ev) override;
    void dragEnterEvent(QDragEnterEvent* e) override;
    void dropEvent(QDropEvent* e) override;
    void closeEvent(QCloseEvent* e) override;

private:
    void buildMenusAndToolbar();
    QWidget* buildRightPanel();
    QWidget* buildBottomBar();
    void openDialog();
    void exportImage();
    bool runExport(const ExportSettings& s, bool interactive, QString* error, QString* warning);
    void chooseDisplayProfile();
    void updateTitle();
    void updateHistoryActions();
    void setToolAction(Tool t);

    EditorSession* session_;
    CanvasWidget* canvas_;
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
    QAction* undoAct_;
    QAction* redoAct_;
    QAction* exportAct_;
    QAction* clipAct_;
    QAction* beforeAct_;
    QActionGroup* toolGroup_;
    QAction* toolActs_[5];
};

}  // namespace re
