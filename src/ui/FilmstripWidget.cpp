#include "ui/FilmstripWidget.h"
#include <QFileInfo>
#include <QHelpEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QToolTip>
#include <QWheelEvent>
#include <algorithm>

namespace re {

namespace {
constexpr int kMargin = 6;     // around the strip
constexpr int kSpacing = 6;    // between cells
constexpr int kCellW = 120;
constexpr int kThumbH = 70;
constexpr int kTextH = 16;
constexpr int kCellH = kThumbH + kTextH + 6;
constexpr int kCloseSize = 14;
}  // namespace

FilmstripWidget::FilmstripWidget(EditorSession* session, QWidget* parent) : QWidget(parent), session_(session) {
    setMouseTracking(true);
    setFixedHeight(kCellH + 2 * kMargin);
    setAutoFillBackground(false);
    connect(session_, &EditorSession::documentsChanged, this, [this] { clampScroll(); update(); });
    connect(session_, &EditorSession::activeChanged, this, [this](int i) { ensureVisible(i); update(); });
    connect(session_, &EditorSession::selectionChanged, this, [this] { update(); });
    connect(session_, &EditorSession::paramsChanged, this, [this](const EditParams&, bool interactive) { if (!interactive) update(); });
}

QSize FilmstripWidget::sizeHint() const { return {kCellW * 4, kCellH + 2 * kMargin}; }

int FilmstripWidget::contentWidth() const {
    int n = session_->documentCount();
    return n > 0 ? n * kCellW + (n - 1) * kSpacing : 0;
}

QRect FilmstripWidget::cellRect(int i) const {
    int x = kMargin + i * (kCellW + kSpacing) - scroll_;
    // Centre the strip when everything fits.
    int slack = width() - 2 * kMargin - contentWidth();
    if (slack > 0) x += slack / 2;
    return QRect(x, kMargin, kCellW, kCellH);
}

QRect FilmstripWidget::closeRect(const QRect& cell) const {
    return QRect(cell.right() - kCloseSize - 3, cell.top() + 3, kCloseSize, kCloseSize);
}

int FilmstripWidget::cellAt(QPoint p) const {
    for (int i = 0; i < session_->documentCount(); ++i)
        if (cellRect(i).contains(p)) return i;
    return -1;
}

void FilmstripWidget::clampScroll() {
    int maxScroll = std::max(0, contentWidth() - (width() - 2 * kMargin));
    scroll_ = std::clamp(scroll_, 0, maxScroll);
}

void FilmstripWidget::ensureVisible(int i) {
    if (i < 0 || i >= session_->documentCount()) return;
    QRect r = cellRect(i);
    if (r.left() < kMargin) scroll_ -= kMargin - r.left();
    else if (r.right() > width() - kMargin) scroll_ += r.right() - (width() - kMargin);
    clampScroll();
}

void FilmstripWidget::resizeEvent(QResizeEvent*) {
    clampScroll();
    ensureVisible(session_->activeIndex());
}

void FilmstripWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), QColor(34, 34, 34));
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    const int active = session_->activeIndex();
    const int pending = session_->pendingActiveIndex();
    QFont small = font();
    small.setPointSizeF(std::max(7.0, font().pointSizeF() - 1));
    for (int i = 0; i < session_->documentCount(); ++i) {
        const ImageDocument& d = session_->document(i);
        QRect cell = cellRect(i);
        if (cell.right() < 0 || cell.left() > width()) continue;
        // cell background
        const bool selected = session_->isSelected(i);
        if (i == active) p.fillRect(cell, QColor(70, 120, 200, 70));
        else if (selected) p.fillRect(cell, QColor(70, 120, 200, 40));
        else if (i == hover_) p.fillRect(cell, QColor(255, 255, 255, 18));
        QString badge;
        QColor badgeColour(230, 230, 230);
        switch (d.state) {
            case ImageDocument::State::Queued: badge = QStringLiteral("queued"); break;
            case ImageDocument::State::Decoding: badge = QStringLiteral("decoding…"); break;
            case ImageDocument::State::Failed: badge = QStringLiteral("failed"); badgeColour = QColor(255, 120, 110); break;
            case ImageDocument::State::Ready: break;
        }
        // thumbnail box: the preview, or the file type while there is none (and no state badge to show instead)
        QRect box(cell.left() + 3, cell.top() + 3, cell.width() - 6, kThumbH);
        p.fillRect(box, QColor(24, 24, 24));
        if (!d.thumbnail.isNull()) {
            QSize s = d.thumbnail.size().scaled(box.size(), Qt::KeepAspectRatio);
            QRect target(QPoint(box.center().x() - s.width() / 2, box.center().y() - s.height() / 2), s);
            p.drawImage(target, d.thumbnail);
        } else if (badge.isEmpty()) {
            p.setPen(QColor(110, 110, 110));
            p.setFont(small);
            p.drawText(box, Qt::AlignCenter, QFileInfo(d.path).suffix().toUpper());
        }
        // edited marker: settings differ from the as-shot defaults (which is also when a sidecar exists)
        if (d.ready() && !(d.params == d.defaults)) {
            p.setPen(QPen(QColor(0, 0, 0, 170), 1));
            p.setBrush(QColor(110, 160, 240));
            p.drawEllipse(QPointF(box.left() + 8, box.top() + 8), 3.5, 3.5);
            p.setBrush(Qt::NoBrush);  // the borders below are outlines only
        }
        // state overlay
        if (!badge.isEmpty()) {
            p.fillRect(box, QColor(0, 0, 0, d.state == ImageDocument::State::Failed ? 90 : 130));
            p.setFont(small);
            p.setPen(badgeColour);
            p.drawText(box, Qt::AlignCenter, badge);
        }
        // borders
        if (i == active) {
            p.setPen(QPen(QColor(110, 160, 240), 2));
            p.drawRect(QRectF(cell).adjusted(1, 1, -1, -1));
        } else if (i == pending) {
            p.setPen(QPen(QColor(110, 160, 240, 180), 1.5, Qt::DashLine));
            p.drawRect(QRectF(cell).adjusted(1, 1, -1, -1));
        } else if (selected) {
            p.setPen(QPen(QColor(110, 160, 240, 150), 1));
            p.drawRect(QRectF(cell).adjusted(0.5, 0.5, -0.5, -0.5));
        }
        // file name
        p.setFont(small);
        p.setPen(i == active ? QColor(245, 245, 245) : QColor(190, 190, 190));
        QRect textRect(cell.left() + 4, box.bottom() + 2, cell.width() - 8, kTextH);
        p.drawText(textRect, Qt::AlignCenter | Qt::AlignVCenter, p.fontMetrics().elidedText(d.fileName(), Qt::ElideMiddle, textRect.width()));
        // close button on hover
        if (i == hover_) {
            QRect c = closeRect(cell);
            p.setPen(Qt::NoPen);
            p.setBrush(hoverClose_ ? QColor(220, 70, 60) : QColor(0, 0, 0, 170));
            p.drawEllipse(c);
            p.setPen(QPen(Qt::white, 1.5));
            p.drawLine(c.center() + QPoint(-3, -3), c.center() + QPoint(4, 4));
            p.drawLine(c.center() + QPoint(4, -3), c.center() + QPoint(-3, 4));
        }
    }
}

void FilmstripWidget::mousePressEvent(QMouseEvent* e) {
    if (e->button() != Qt::LeftButton) return;
    int i = cellAt(e->pos());
    if (i < 0) return;
    if (closeRect(cellRect(i)).contains(e->pos())) {
        session_->closeDocument(i);
        hover_ = cellAt(e->pos());
        hoverClose_ = hover_ >= 0 && closeRect(cellRect(hover_)).contains(e->pos());
        update();
        return;
    }
    if (e->modifiers() & Qt::ControlModifier) session_->toggleSelected(i);
    else if (e->modifiers() & Qt::ShiftModifier) session_->selectRangeTo(i);
    else session_->setActiveIndex(i);
}

void FilmstripWidget::mouseMoveEvent(QMouseEvent* e) {
    int i = cellAt(e->pos());
    bool onClose = i >= 0 && closeRect(cellRect(i)).contains(e->pos());
    if (i != hover_ || onClose != hoverClose_) {
        hover_ = i;
        hoverClose_ = onClose;
        setCursor(i >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
        update();
    }
}

void FilmstripWidget::leaveEvent(QEvent*) {
    if (hover_ >= 0) { hover_ = -1; hoverClose_ = false; update(); }
}

void FilmstripWidget::wheelEvent(QWheelEvent* e) {
    int delta = e->angleDelta().y() != 0 ? e->angleDelta().y() : e->angleDelta().x();
    if (delta == 0) return;
    scroll_ -= delta / 120 * (kCellW + kSpacing) / 2;
    clampScroll();
    update();
    e->accept();
}

bool FilmstripWidget::event(QEvent* ev) {
    if (ev->type() == QEvent::ToolTip) {
        auto* he = static_cast<QHelpEvent*>(ev);
        int i = cellAt(he->pos());
        if (i >= 0) {
            const ImageDocument& d = session_->document(i);
            QString tip = d.path;
            if (d.state == ImageDocument::State::Failed) tip += QStringLiteral("\n%1").arg(d.error);
            else if (d.image) tip += QStringLiteral("\n%1 × %2").arg(d.image->width).arg(d.image->height);
            if (d.ready() && !(d.params == d.defaults)) tip += QStringLiteral("\nedited");
            tip += QStringLiteral("\nCtrl-click toggles, Shift-click extends the selection");
            QToolTip::showText(he->globalPos(), tip, this);
        } else {
            QToolTip::hideText();
        }
        return true;
    }
    return QWidget::event(ev);
}

}  // namespace re
