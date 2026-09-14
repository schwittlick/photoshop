#pragma once
// Thumbnail strip of the loaded images: click to switch, the corner × closes, the wheel
// scrolls. Thumbnails are the raws' embedded previews, so they show the file, not the edit.
#include "ui/EditorSession.h"
#include <QWidget>

namespace re {

class FilmstripWidget : public QWidget {
    Q_OBJECT
public:
    explicit FilmstripWidget(EditorSession* session, QWidget* parent = nullptr);
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void leaveEvent(QEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    void resizeEvent(QResizeEvent*) override;
    bool event(QEvent*) override;

private:
    QRect cellRect(int index) const;     // widget coordinates, scroll applied
    QRect closeRect(const QRect& cell) const;
    int cellAt(QPoint p) const;
    int contentWidth() const;
    void clampScroll();
    void ensureVisible(int index);

    EditorSession* session_;
    int scroll_ = 0;
    int hover_ = -1;
    bool hoverClose_ = false;
};

}  // namespace re
