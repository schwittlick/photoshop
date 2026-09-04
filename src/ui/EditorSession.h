#pragma once
// Owns the loaded image, the lens profile data, the current EditParams and the
// undo history. Panels and tools talk to the session; the canvas listens to it.
#include "core/EditParams.h"
#include "core/History.h"
#include "core/LensModel.h"
#include "io/MetadataReader.h"
#include "io/RawImage.h"
#include <QFutureWatcher>
#include <QObject>
#include <memory>

namespace re {

struct LoadResult {
    std::shared_ptr<RawImage> image;
    ImageMetadata meta;
    QString error;
    QString path;
};

class EditorSession : public QObject {
    Q_OBJECT
public:
    explicit EditorSession(QObject* parent = nullptr);
    ~EditorSession() override;

    void openFile(const QString& path);  // asynchronous decode on a worker thread
    bool isLoading() const { return loading_; }
    bool hasImage() const { return image_ != nullptr; }
    std::shared_ptr<const RawImage> image() const { return image_; }
    const ImageMetadata& metadata() const { return meta_; }
    const QString& filePath() const { return path_; }
    const LensModel& lensModel() const { return lens_; }
    const LensGrid& lensGrid() const { return grid_; }
    const VignetteLut& vignetteLut() const { return vig_; }
    QString lensStatus() const;
    QString imageDescription() const;

    const EditParams& params() const { return params_; }
    const EditParams& defaults() const { return defaults_; }
    // interactive = mid-drag: re-render but do not record history. Release commits.
    void setParams(const EditParams& p, bool interactive);
    void commit();
    void resetAll();
    void undo();
    void redo();
    bool canUndo() const { return history_.canUndo(); }
    bool canRedo() const { return history_.canRedo(); }

    // Eyedropper: neutralise the given normalised raw RGB.
    void setWhiteBalanceFromRaw(const Vec3& rawNormalised);
    void setWhiteBalanceAsShot();

signals:
    void loadStarted(const QString& path);
    void loadFinished(bool ok, const QString& error);
    void imageChanged();
    void paramsChanged(const EditParams& params, bool interactive);
    void historyChanged();

private:
    void onLoadFinished();

    std::shared_ptr<RawImage> image_;
    ImageMetadata meta_;
    QString path_;
    LensModel lens_;
    LensGrid grid_;
    VignetteLut vig_;
    EditParams params_, defaults_;
    History history_;
    QFutureWatcher<LoadResult> watcher_;
    bool loading_ = false;
};

}  // namespace re
