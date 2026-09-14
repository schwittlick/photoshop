#include "ui/EditorSession.h"
#include "io/RawLoader.h"
#include "io/Sidecar.h"
#include <QFileInfo>
#include <QtConcurrent/QtConcurrent>
#include <algorithm>
#include <cmath>

namespace re {

namespace {
constexpr int kThumbEdge = 256;
const EditParams kNoParams;
const ImageMetadata kNoMeta;
const LensGrid kNoGrid;
const VignetteLut kNoVig;
const LensModel& noLens() { static LensModel m; return m; }
}  // namespace

QString ImageDocument::fileName() const { return QFileInfo(path).fileName(); }

EditorSession::EditorSession(QObject* parent) : QObject(parent) {
    connect(&watcher_, &QFutureWatcher<LoadResult>::finished, this, &EditorSession::onLoadFinished);
    saveTimer_.setSingleShot(true);
    saveTimer_.setInterval(250);
    connect(&saveTimer_, &QTimer::timeout, this, &EditorSession::flushSidecars);
}

EditorSession::~EditorSession() {
    flushSidecars();
    if (decodingId_ >= 0) watcher_.waitForFinished();
    for (auto& f : thumbFutures_) f.waitForFinished();
}

// ------------------------------------------------------------------ documents

ImageDocument* EditorSession::active() { return active_ >= 0 ? docs_[size_t(active_)].get() : nullptr; }
const ImageDocument* EditorSession::active() const { return active_ >= 0 ? docs_[size_t(active_)].get() : nullptr; }

int EditorSession::indexOfId(int id) const {
    if (id < 0) return -1;
    for (size_t i = 0; i < docs_.size(); ++i)
        if (docs_[i]->id == id) return int(i);
    return -1;
}

int EditorSession::indexOfPath(const QString& path) const {
    for (size_t i = 0; i < docs_.size(); ++i)
        if (docs_[i]->path == path) return int(i);
    return -1;
}

int EditorSession::pendingActiveIndex() const { return indexOfId(pendingActiveId_); }

int EditorSession::readyCount() const {
    return int(std::count_if(docs_.begin(), docs_.end(), [](const auto& d) { return d->ready(); }));
}

void EditorSession::openFiles(const QStringList& paths) {
    int firstNew = -1, firstExisting = -1;
    for (const QString& raw : paths) {
        if (raw.isEmpty()) continue;
        const QString path = QFileInfo(raw).absoluteFilePath();
        int idx = indexOfPath(path);
        if (idx >= 0) { if (firstExisting < 0) firstExisting = idx; continue; }
        auto d = std::make_unique<ImageDocument>();
        d->id = nextId_++;
        d->path = path;
        docs_.push_back(std::move(d));
        if (firstNew < 0) firstNew = int(docs_.size()) - 1;
        requestThumbnail(*docs_.back());
    }
    if (firstNew < 0) {
        if (firstExisting >= 0) setActiveIndex(firstExisting);  // re-opening a loaded file just selects it
        return;
    }
    // What was just opened is what the user wants to see: show the first new file once it is decoded.
    pendingActiveId_ = docs_[size_t(firstNew)]->id;
    emit documentsChanged();
    pumpQueue();
}

bool EditorSession::pumpQueue() {
    if (decodingId_ >= 0) return true;
    ImageDocument* next = nullptr;
    int pi = indexOfId(pendingActiveId_);
    if (pi >= 0 && docs_[size_t(pi)]->state == ImageDocument::State::Queued) next = docs_[size_t(pi)].get();
    for (auto& d : docs_) {
        if (next) break;
        if (d->state == ImageDocument::State::Queued) next = d.get();
    }
    if (!next) return false;
    next->state = ImageDocument::State::Decoding;
    decodingId_ = next->id;
    const QString path = next->path;
    const int id = next->id;
    emit loadStarted(path);
    emit documentsChanged();
    watcher_.setFuture(QtConcurrent::run([path, id]() {
        LoadResult r;
        r.id = id;
        r.path = path;
        r.image = RawLoader::load(path, &r.error);
        r.meta = readMetadata(path);
        return r;
    }));
    return true;
}

void EditorSession::onLoadFinished() {
    LoadResult r = watcher_.result();
    decodingId_ = -1;
    int idx = indexOfId(r.id);
    if (idx < 0) {  // closed while decoding
        if (!pumpQueue()) emit allLoadsFinished();
        return;
    }
    ImageDocument& d = *docs_[size_t(idx)];
    if (!r.image) {
        d.state = ImageDocument::State::Failed;
        d.error = r.error;
        if (pendingActiveId_ == d.id) pendingActiveId_ = -1;
        emit documentsChanged();
        emit loadFinished(false, QStringLiteral("%1: %2").arg(d.fileName(), r.error));
        if (!pumpQueue()) emit allLoadsFinished();
        return;
    }
    d.image = r.image;
    d.meta = r.meta;

    // Lens profile lookup: exiv2 metadata first, LibRaw's fields as fallback.
    LensQuery q;
    q.cameraMake = d.meta.make.isEmpty() ? d.image->make : d.meta.make;
    q.cameraModel = d.meta.model.isEmpty() ? d.image->model : d.meta.model;
    q.lensModel = d.meta.lensModel.isEmpty() ? d.image->lens : d.meta.lensModel;
    q.focal = d.meta.focalLength > 0 ? d.meta.focalLength : d.image->focal;
    q.aperture = d.meta.fNumber > 0 ? d.meta.fNumber : d.image->aperture;
    q.distance = d.meta.subjectDistance;
    double f35 = d.meta.focalLength35 > 0 ? d.meta.focalLength35 : d.image->focal35;
    if (q.focal > 0 && f35 > 0) q.cropFactorHint = f35 / q.focal;
    d.lens = std::make_unique<LensModel>();
    d.lens->match(q);
    d.grid = d.lens->hasProfile() ? d.lens->buildGrid(d.image->width, d.image->height, true, true) : LensGrid();
    d.vig = d.lens->hasProfile() ? d.lens->buildVignette(d.image->width, d.image->height) : VignetteLut();

    // Defaults: as-shot white balance, everything else neutral.
    d.defaults = EditParams();
    auto [T, tint] = d.image->camera.tempTintFromMultipliers(d.image->asShotMultipliers());
    d.defaults.wb.temp = float(std::round(T));
    d.defaults.wb.tint = float(std::round(tint * 10.0) / 10.0);
    d.params = d.defaults;
    QString sidecarError;
    if (sidecar::read(d.path, &d.params, &sidecarError) == sidecar::ReadResult::Invalid) {
        d.sidecarLocked = true;
        emit sidecarProblem(QStringLiteral("%1: %2 — file kept, edits of this image are not saved").arg(QFileInfo(sidecar::pathFor(d.path)).fileName(), sidecarError));
    }
    d.history.reset(d.params);
    if (d.pendingSync) {
        d.params = applySync(d.pendingSync->first, d.params, d.pendingSync->second);
        d.history.commit(d.params);
        d.pendingSync.reset();
        persist(d);
    }
    d.state = ImageDocument::State::Ready;
    emit documentsChanged();
    if (pendingActiveId_ == d.id || active_ < 0) {
        pendingActiveId_ = -1;
        activate(idx);
    }
    emit loadFinished(true, {});
    if (!pumpQueue()) emit allLoadsFinished();
}

void EditorSession::activate(int index) {
    active_ = index;
    selectOnly(index);
    emit activeChanged(index);
    emit imageChanged();
    emit paramsChanged(params(), false);
    emit historyChanged();
}

void EditorSession::announceNoImage() {
    active_ = -1;
    for (auto& d : docs_) d->selected = false;
    emit selectionChanged();
    emit activeChanged(-1);
    emit imageChanged();
    emit paramsChanged(params(), false);
    emit historyChanged();
}

void EditorSession::setActiveIndex(int index) {
    if (index < 0 || index >= documentCount()) return;
    ImageDocument& d = *docs_[size_t(index)];
    switch (d.state) {
        case ImageDocument::State::Ready:
            pendingActiveId_ = -1;  // also cancels a pending switch when the current image is clicked again
            if (index != active_) activate(index);
            emit documentsChanged();
            break;
        case ImageDocument::State::Queued:
        case ImageDocument::State::Decoding:
            pendingActiveId_ = d.id;
            emit documentsChanged();
            pumpQueue();
            break;
        case ImageDocument::State::Failed:
            break;  // nothing to show; the filmstrip carries the error
    }
}

void EditorSession::closeDocument(int index) {
    if (index < 0 || index >= documentCount()) return;
    flushSidecars();
    if (docs_[size_t(index)]->id == pendingActiveId_) pendingActiveId_ = -1;
    // A decode in flight cannot be cancelled; its result is dropped when it arrives with an unknown id.
    const bool wasActive = index == active_;
    const bool wasSelected = docs_[size_t(index)]->selected;
    docs_.erase(docs_.begin() + index);
    if (wasSelected && !wasActive) emit selectionChanged();
    if (wasActive) {
        active_ = -1;
        int pick = -1;
        for (int i = index; i < documentCount() && pick < 0; ++i) if (docs_[size_t(i)]->ready()) pick = i;
        for (int i = index - 1; i >= 0 && pick < 0; --i) if (docs_[size_t(i)]->ready()) pick = i;
        emit documentsChanged();
        if (pick >= 0) activate(pick);
        else announceNoImage();
        return;
    }
    if (active_ > index) --active_;
    emit documentsChanged();
}

void EditorSession::closeAll() {
    if (docs_.empty()) return;
    flushSidecars();
    docs_.clear();
    pendingActiveId_ = -1;
    emit documentsChanged();
    announceNoImage();
}

void EditorSession::requestThumbnail(const ImageDocument& d) {
    const QString path = d.path;
    const int id = d.id;
    std::erase_if(thumbFutures_, [](const QFuture<QImage>& f) { return f.isFinished(); });
    auto* w = new QFutureWatcher<QImage>(this);
    connect(w, &QFutureWatcher<QImage>::finished, this, [this, w, id] {
        int idx = indexOfId(id);
        if (idx >= 0) {
            docs_[size_t(idx)]->thumbnail = w->result();
            emit documentsChanged();
        }
        w->deleteLater();
    });
    QFuture<QImage> f = QtConcurrent::run([path]() { return RawLoader::loadThumbnail(path, kThumbEdge); });
    thumbFutures_.push_back(f);
    w->setFuture(f);
}

// ------------------------------------------------------------------ selection

void EditorSession::selectOnly(int index) {
    for (size_t i = 0; i < docs_.size(); ++i) docs_[i]->selected = int(i) == index;
    emit selectionChanged();
}

int EditorSession::selectedCount() const {
    return int(std::count_if(docs_.begin(), docs_.end(), [](const auto& d) { return d->selected; }));
}

std::vector<int> EditorSession::selectedIndices() const {
    std::vector<int> out;
    for (size_t i = 0; i < docs_.size(); ++i)
        if (docs_[i]->selected) out.push_back(int(i));
    return out;
}

void EditorSession::toggleSelected(int index) {
    if (index < 0 || index >= documentCount() || index == active_) return;
    docs_[size_t(index)]->selected = !docs_[size_t(index)]->selected;
    emit selectionChanged();
}

void EditorSession::selectRangeTo(int index) {
    if (index < 0 || index >= documentCount()) return;
    int anchor = active_ >= 0 ? active_ : (pendingActiveIndex() >= 0 ? pendingActiveIndex() : index);
    const int lo = std::min(anchor, index), hi = std::max(anchor, index);
    for (size_t i = 0; i < docs_.size(); ++i) docs_[i]->selected = (int(i) >= lo && int(i) <= hi) || int(i) == active_;
    emit selectionChanged();
}

void EditorSession::selectAll() {
    if (docs_.empty()) return;
    for (auto& d : docs_) d->selected = true;
    emit selectionChanged();
}

// ------------------------------------------------------------------ active document

std::shared_ptr<const RawImage> EditorSession::image() const { const auto* d = active(); return d ? d->image : nullptr; }
const ImageMetadata& EditorSession::metadata() const { const auto* d = active(); return d ? d->meta : kNoMeta; }
QString EditorSession::filePath() const { const auto* d = active(); return d ? d->path : QString(); }
const LensModel& EditorSession::lensModel() const { const auto* d = active(); return d && d->lens ? *d->lens : noLens(); }
const LensGrid& EditorSession::lensGrid() const { const auto* d = active(); return d ? d->grid : kNoGrid; }
const VignetteLut& EditorSession::vignetteLut() const { const auto* d = active(); return d ? d->vig : kNoVig; }
const EditParams& EditorSession::params() const { const auto* d = active(); return d ? d->params : kNoParams; }
const EditParams& EditorSession::defaults() const { const auto* d = active(); return d ? d->defaults : kNoParams; }
bool EditorSession::canUndo() const { const auto* d = active(); return d && d->history.canUndo(); }
bool EditorSession::canRedo() const { const auto* d = active(); return d && d->history.canRedo(); }

QString EditorSession::lensStatus() const {
    const auto* d = active();
    if (!d || !d->lens) return {};
    if (d->lens->hasProfile()) return d->lens->statusText();
    return d->lens->statusText().isEmpty() ? QStringLiteral("no lens profile") : d->lens->statusText();
}

QString EditorSession::imageDescription() const {
    const auto* d = active();
    if (!d) return {};
    const ImageMetadata& meta = d->meta;
    const RawImage& img = *d->image;
    QString cam = QStringLiteral("%1 %2").arg(meta.make.isEmpty() ? img.make : meta.make, meta.model.isEmpty() ? img.model : meta.model).trimmed();
    QString exp;
    double sh = meta.exposureTime > 0 ? meta.exposureTime : img.shutter;
    if (sh > 0) exp = sh >= 1 ? QStringLiteral("%1 s").arg(sh, 0, 'g', 3) : QStringLiteral("1/%1 s").arg(std::lround(1.0 / sh));
    double fn = meta.fNumber > 0 ? meta.fNumber : img.aperture;
    double fl = meta.focalLength > 0 ? meta.focalLength : img.focal;
    int iso = meta.iso > 0 ? meta.iso : img.iso;
    QStringList parts{d->fileName(), cam, QStringLiteral("%1 × %2").arg(img.width).arg(img.height)};
    if (fl > 0) parts << QStringLiteral("%1 mm").arg(fl, 0, 'f', fl < 10 ? 1 : 0);
    if (fn > 0) parts << QStringLiteral("f/%1").arg(fn, 0, 'g', 3);
    if (!exp.isEmpty()) parts << exp;
    if (iso > 0) parts << QStringLiteral("ISO %1").arg(iso);
    return parts.join(QStringLiteral("  ·  "));
}

void EditorSession::setParams(const EditParams& p, bool interactive) {
    auto* d = active();
    if (!d) return;
    d->params = p;
    emit paramsChanged(d->params, interactive);
    if (!interactive) commit();
}

void EditorSession::commit() {
    auto* d = active();
    if (!d) return;
    d->history.commit(d->params);
    emit historyChanged();
    persist(*d);
}

void EditorSession::resetAll() {
    if (const auto* d = active()) setParams(d->defaults, false);
}

void EditorSession::undo() {
    auto* d = active();
    if (!d || !d->history.canUndo()) return;
    d->params = d->history.undo();
    emit paramsChanged(d->params, false);
    emit historyChanged();
    persist(*d);
}

void EditorSession::redo() {
    auto* d = active();
    if (!d || !d->history.canRedo()) return;
    d->params = d->history.redo();
    emit paramsChanged(d->params, false);
    emit historyChanged();
    persist(*d);
}

void EditorSession::setWhiteBalanceFromRaw(const Vec3& raw) {
    const auto* d = active();
    if (!d) return;
    Vec3 mul(1.0 / std::max(raw.x, 1e-4), 1.0 / std::max(raw.y, 1e-4), 1.0 / std::max(raw.z, 1e-4));
    mul = mul / mul.y;
    auto [T, tint] = d->image->camera.tempTintFromMultipliers(mul);
    EditParams p = d->params;
    p.wb.temp = float(std::clamp(T, colour::kTempMin, colour::kTempMax));
    p.wb.tint = float(std::clamp(tint, -150.0, 150.0));
    setParams(p, false);
}

void EditorSession::setWhiteBalanceAsShot() {
    const auto* d = active();
    if (!d) return;
    EditParams p = d->params;
    p.wb = d->defaults.wb;
    setParams(p, false);
}

int EditorSession::syncToAll(const SyncMask& mask) {
    const ImageDocument* src = active();
    if (!src || !mask.any()) return 0;
    int changed = 0;
    for (auto& d : docs_) {
        if (d.get() == src) continue;
        if (d->ready()) {
            EditParams p = applySync(src->params, d->params, mask);
            if (p == d->params) continue;
            d->params = p;
            d->history.commit(p);
            persist(*d);
            ++changed;
        } else if (d->state != ImageDocument::State::Failed) {
            d->pendingSync = std::make_pair(src->params, mask);
            ++changed;
        }
    }
    if (changed) emit documentsChanged();
    return changed;
}

// ------------------------------------------------------------------ sidecars

void EditorSession::persist(const ImageDocument& d) {
    if (std::find(dirtyIds_.begin(), dirtyIds_.end(), d.id) == dirtyIds_.end()) dirtyIds_.push_back(d.id);
    saveTimer_.start();
}

void EditorSession::flushSidecars() {
    saveTimer_.stop();
    std::vector<int> ids;
    ids.swap(dirtyIds_);
    for (int id : ids) {
        int idx = indexOfId(id);
        if (idx < 0) continue;
        const ImageDocument& d = *docs_[size_t(idx)];
        if (!d.ready() || d.sidecarLocked) continue;
        QString err;
        const bool ok = d.params == d.defaults ? sidecar::remove(d.path, &err) : sidecar::write(d.path, d.params, &err);
        if (!ok) emit sidecarProblem(QStringLiteral("Could not save %1: %2").arg(QFileInfo(sidecar::pathFor(d.path)).fileName(), err));
    }
}

}  // namespace re
