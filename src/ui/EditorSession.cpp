#include "ui/EditorSession.h"
#include "io/RawLoader.h"
#include <QFileInfo>
#include <QtConcurrent/QtConcurrent>
#include <cmath>

namespace re {

EditorSession::EditorSession(QObject* parent) : QObject(parent) {
    connect(&watcher_, &QFutureWatcher<LoadResult>::finished, this, &EditorSession::onLoadFinished);
    history_.reset(params_);
}

EditorSession::~EditorSession() {
    if (loading_) watcher_.waitForFinished();
}

void EditorSession::openFile(const QString& path) {
    if (loading_) return;
    loading_ = true;
    emit loadStarted(path);
    watcher_.setFuture(QtConcurrent::run([path]() {
        LoadResult r;
        r.path = path;
        r.image = RawLoader::load(path, &r.error);
        r.meta = readMetadata(path);
        return r;
    }));
}

void EditorSession::onLoadFinished() {
    loading_ = false;
    LoadResult r = watcher_.result();
    if (!r.image) {
        emit loadFinished(false, r.error);
        return;
    }
    image_ = r.image;
    meta_ = r.meta;
    path_ = r.path;

    // Lens profile lookup: exiv2 metadata first, LibRaw's fields as fallback.
    LensQuery q;
    q.cameraMake = meta_.make.isEmpty() ? image_->make : meta_.make;
    q.cameraModel = meta_.model.isEmpty() ? image_->model : meta_.model;
    q.lensModel = meta_.lensModel.isEmpty() ? image_->lens : meta_.lensModel;
    q.focal = meta_.focalLength > 0 ? meta_.focalLength : image_->focal;
    q.aperture = meta_.fNumber > 0 ? meta_.fNumber : image_->aperture;
    q.distance = meta_.subjectDistance;
    double f35 = meta_.focalLength35 > 0 ? meta_.focalLength35 : image_->focal35;
    if (q.focal > 0 && f35 > 0) q.cropFactorHint = f35 / q.focal;
    lens_.match(q);
    grid_ = lens_.hasProfile() ? lens_.buildGrid(image_->width, image_->height, true, true) : LensGrid();
    vig_ = lens_.hasProfile() ? lens_.buildVignette(image_->width, image_->height) : VignetteLut();

    // Defaults: as-shot white balance, everything else neutral.
    defaults_ = EditParams();
    auto [T, tint] = image_->camera.tempTintFromMultipliers(image_->asShotMultipliers());
    defaults_.wb.temp = float(std::round(T));
    defaults_.wb.tint = float(std::round(tint * 10.0) / 10.0);
    params_ = defaults_;
    history_.reset(params_);
    emit imageChanged();
    emit paramsChanged(params_, false);
    emit historyChanged();
    emit loadFinished(true, {});
}

QString EditorSession::lensStatus() const {
    if (!image_) return {};
    if (lens_.hasProfile()) return lens_.statusText();
    return lens_.statusText().isEmpty() ? QStringLiteral("no lens profile") : lens_.statusText();
}

QString EditorSession::imageDescription() const {
    if (!image_) return {};
    QString cam = QStringLiteral("%1 %2").arg(meta_.make.isEmpty() ? image_->make : meta_.make, meta_.model.isEmpty() ? image_->model : meta_.model).trimmed();
    QString exp;
    double sh = meta_.exposureTime > 0 ? meta_.exposureTime : image_->shutter;
    if (sh > 0) exp = sh >= 1 ? QStringLiteral("%1 s").arg(sh, 0, 'g', 3) : QStringLiteral("1/%1 s").arg(std::lround(1.0 / sh));
    double fn = meta_.fNumber > 0 ? meta_.fNumber : image_->aperture;
    double fl = meta_.focalLength > 0 ? meta_.focalLength : image_->focal;
    int iso = meta_.iso > 0 ? meta_.iso : image_->iso;
    QStringList parts{QFileInfo(path_).fileName(), cam, QStringLiteral("%1 × %2").arg(image_->width).arg(image_->height)};
    if (fl > 0) parts << QStringLiteral("%1 mm").arg(fl, 0, 'f', fl < 10 ? 1 : 0);
    if (fn > 0) parts << QStringLiteral("f/%1").arg(fn, 0, 'g', 3);
    if (!exp.isEmpty()) parts << exp;
    if (iso > 0) parts << QStringLiteral("ISO %1").arg(iso);
    return parts.join(QStringLiteral("  ·  "));
}

void EditorSession::setParams(const EditParams& p, bool interactive) {
    params_ = p;
    emit paramsChanged(params_, interactive);
    if (!interactive) commit();
}

void EditorSession::commit() {
    history_.commit(params_);
    emit historyChanged();
}

void EditorSession::resetAll() { setParams(defaults_, false); }

void EditorSession::undo() {
    if (!history_.canUndo()) return;
    params_ = history_.undo();
    emit paramsChanged(params_, false);
    emit historyChanged();
}

void EditorSession::redo() {
    if (!history_.canRedo()) return;
    params_ = history_.redo();
    emit paramsChanged(params_, false);
    emit historyChanged();
}

void EditorSession::setWhiteBalanceFromRaw(const Vec3& raw) {
    if (!image_) return;
    Vec3 mul(1.0 / std::max(raw.x, 1e-4), 1.0 / std::max(raw.y, 1e-4), 1.0 / std::max(raw.z, 1e-4));
    mul = mul / mul.y;
    auto [T, tint] = image_->camera.tempTintFromMultipliers(mul);
    EditParams p = params_;
    p.wb.temp = float(std::clamp(T, colour::kTempMin, colour::kTempMax));
    p.wb.tint = float(std::clamp(tint, -150.0, 150.0));
    setParams(p, false);
}

void EditorSession::setWhiteBalanceAsShot() {
    EditParams p = params_;
    p.wb = defaults_.wb;
    setParams(p, false);
}

}  // namespace re
