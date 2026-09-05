#include "ui/MainWindow.h"
#include <QApplication>
#include <QIcon>
#include <QPalette>
#include <QStyleFactory>
#include <QSurfaceFormat>

int main(int argc, char** argv) {
    QSurfaceFormat fmt;
    fmt.setVersion(4, 3);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    fmt.setDepthBufferSize(24);
    fmt.setStencilBufferSize(8);
    fmt.setSwapInterval(1);
    QSurfaceFormat::setDefaultFormat(fmt);

    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("photoshop"));
    app.setDesktopFileName(QStringLiteral("photoshop"));
    app.setWindowIcon(QIcon::fromTheme(QStringLiteral("photoshop")));
    app.setOrganizationName(QStringLiteral("photoshop"));
    app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    QPalette pal;
    pal.setColor(QPalette::Window, QColor(50, 50, 50));
    pal.setColor(QPalette::WindowText, QColor(220, 220, 220));
    pal.setColor(QPalette::Base, QColor(38, 38, 38));
    pal.setColor(QPalette::AlternateBase, QColor(50, 50, 50));
    pal.setColor(QPalette::ToolTipBase, QColor(60, 60, 60));
    pal.setColor(QPalette::ToolTipText, QColor(220, 220, 220));
    pal.setColor(QPalette::Text, QColor(220, 220, 220));
    pal.setColor(QPalette::Button, QColor(58, 58, 58));
    pal.setColor(QPalette::ButtonText, QColor(220, 220, 220));
    pal.setColor(QPalette::BrightText, Qt::red);
    pal.setColor(QPalette::Highlight, QColor(70, 120, 200));
    pal.setColor(QPalette::HighlightedText, Qt::white);
    pal.setColor(QPalette::Mid, QColor(140, 140, 140));
    pal.setColor(QPalette::Disabled, QPalette::Text, QColor(120, 120, 120));
    pal.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(120, 120, 120));
    pal.setColor(QPalette::Disabled, QPalette::WindowText, QColor(120, 120, 120));
    app.setPalette(pal);

    QString file, screenshot, exportPath;
    bool noLens = false, demo = false;
    QString toolName;
    double zoom = 0;
    const QStringList args = app.arguments();
    for (int i = 1; i < args.size(); ++i) {
        if (args[i] == "--screenshot" && i + 1 < args.size()) screenshot = args[++i];
        else if (args[i] == "--export" && i + 1 < args.size()) exportPath = args[++i];
        else if (args[i] == "--no-lens") noLens = true;
        else if (args[i] == "--demo") demo = true;
        else if (args[i] == "--tool" && i + 1 < args.size()) toolName = args[++i];
        else if (args[i] == "--zoom" && i + 1 < args.size()) zoom = args[++i].toDouble();
        else if (!args[i].startsWith("--")) file = args[i];
    }
    re::MainWindow w;
    w.resize(1500, 950);
    w.show();
    if (!screenshot.isEmpty() || !exportPath.isEmpty() || demo) w.runHeadless(screenshot, exportPath, noLens, demo, toolName, zoom);
    if (!file.isEmpty()) w.openFile(file);
    return app.exec();
}
