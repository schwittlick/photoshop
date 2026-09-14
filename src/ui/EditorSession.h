#pragma once
// Owns the set of loaded images ("documents") and the active one. Every document has its own
// decoded raw, lens profile data, EditParams and undo history. Panels and tools talk to the
// active document through the session; the canvas and the filmstrip listen to it.
#include "core/EditParams.h"
#include "core/History.h"
#include "core/LensModel.h"
#include "io/MetadataReader.h"
#include "io/RawImage.h"
#include <QFuture>
#include <QFutureWatcher>
#include <QImage>
#include <QObject>
#include <QStringList>
#include <QTimer>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace re {

struct LoadResult {
    int id = -1;
    std::shared_ptr<RawImage> image;
    ImageMetadata meta;
    QString error;
    QString path;
};

struct ImageDocument {
    enum class State { Queued, Decoding, Ready, Failed };
    int id = -1;                        // stable across removals, unlike the index
    QString path;
    State state = State::Queued;
    QString error;                      // decode failure
    QImage thumbnail;                   // embedded preview, oriented; null until extracted
    std::shared_ptr<RawImage> image;    // null until decoded
    ImageMetadata meta;
    std::unique_ptr<LensModel> lens;
    LensGrid grid;
    VignetteLut vig;
    EditParams params, defaults;
    History history;
    // A sync requested while this document was still decoding; applied over the defaults once it is ready.
    std::optional<std::pair<EditParams, SyncMask>> pendingSync;
    bool sidecarLocked = false;         // the sidecar on disk could not be read: never overwrite it
    bool selected = false;

    QString fileName() const;
    bool ready() const { return state == State::Ready; }
};

class EditorSession : public QObject {
    Q_OBJECT
public:
    explicit EditorSession(QObject* parent = nullptr);
    ~EditorSession() override;

    // --- documents. Files decode one at a time on a worker thread, in list order (a selected one
    // jumps the queue). The first file of an open request is shown as soon as it is decoded.
    void openFiles(const QStringList& paths);   // adds to the set; an already-open path is just selected
    void openFile(const QString& path) { openFiles({path}); }
    int documentCount() const { return int(docs_.size()); }
    const ImageDocument& document(int index) const { return *docs_[size_t(index)]; }
    int indexOfPath(const QString& path) const;
    int activeIndex() const { return active_; }
    int pendingActiveIndex() const;             // selected but not decoded yet, else -1
    void setActiveIndex(int index);             // a queued document is decoded next and shown when ready
    void closeDocument(int index);
    void closeAll();
    bool isLoading() const { return decodingId_ >= 0; }
    int readyCount() const;

    // --- selection. The active image is always part of it and plain activation selects only that image;
    // Ctrl/Shift clicks in the filmstrip toggle or extend it. Export acts on the selection.
    bool isSelected(int index) const { return docs_[size_t(index)]->selected; }
    int selectedCount() const;
    std::vector<int> selectedIndices() const;
    void toggleSelected(int index);   // the active image cannot be deselected
    void selectRangeTo(int index);    // the selection becomes the range between the active image and `index`
    void selectAll();

    // --- the active document
    bool hasImage() const { return active_ >= 0; }
    std::shared_ptr<const RawImage> image() const;
    const ImageMetadata& metadata() const;
    QString filePath() const;
    const LensModel& lensModel() const;
    const LensGrid& lensGrid() const;
    const VignetteLut& vignetteLut() const;
    QString lensStatus() const;
    QString imageDescription() const;

    const EditParams& params() const;
    const EditParams& defaults() const;
    // interactive = mid-drag: re-render but do not record history. Release commits.
    void setParams(const EditParams& p, bool interactive);
    void commit();
    void resetAll();
    void undo();
    void redo();
    bool canUndo() const;
    bool canRedo() const;

    // Eyedropper: neutralise the given normalised raw RGB.
    void setWhiteBalanceFromRaw(const Vec3& rawNormalised);
    void setWhiteBalanceAsShot();

    // Copy the selected groups of the active image's settings to every other document, one undo step
    // each. Documents still decoding receive them once ready. Returns how many documents were changed.
    int syncToAll(const SyncMask& mask);

    // Edit state persists in a JSON sidecar next to each raw (io/Sidecar.h): loaded on open, written
    // shortly after every change, removed when the image is back at its defaults. Flushed on close.
    void flushSidecars();

signals:
    void loadStarted(const QString& path);       // per file
    void loadFinished(bool ok, const QString& error);
    void allLoadsFinished();                     // the decode queue ran empty
    void documentsChanged();                     // list, states, thumbnails or other documents' params
    void activeChanged(int index);               // -1 = none
    void selectionChanged();
    void sidecarProblem(const QString& message); // a sidecar could not be read or written
    void imageChanged();                         // the active image is another one (or gone)
    void paramsChanged(const EditParams& params, bool interactive);
    void historyChanged();

private:
    ImageDocument* active();
    const ImageDocument* active() const;
    int indexOfId(int id) const;
    bool pumpQueue();                            // starts the next decode; false if nothing is queued
    void onLoadFinished();
    void activate(int index);
    void announceNoImage();
    void selectOnly(int index);
    void requestThumbnail(const ImageDocument& d);
    void persist(const ImageDocument& d);        // schedule a sidecar write for this document

    std::vector<std::unique_ptr<ImageDocument>> docs_;
    int active_ = -1;
    int pendingActiveId_ = -1;
    int decodingId_ = -1;
    int nextId_ = 1;
    QFutureWatcher<LoadResult> watcher_;
    std::vector<QFuture<QImage>> thumbFutures_;
    QTimer saveTimer_;
    std::vector<int> dirtyIds_;
};

}  // namespace re
