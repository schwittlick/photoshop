#pragma once
#include "io/RawImage.h"
#include <QImage>
#include <QString>
#include <QStringList>
#include <memory>

namespace re {

class RawLoader {
public:
    // Blocking decode; run on a worker thread. Returns null and fills *error on failure.
    static std::shared_ptr<RawImage> load(const QString& path, QString* error);
    // Fast preview from the embedded JPEG/bitmap thumbnail, oriented like the decoded image and
    // scaled so the long edge is `maxEdge`. Null when the file carries no usable preview.
    static QImage loadThumbnail(const QString& path, int maxEdge, QString* error = nullptr);
    static QStringList supportedExtensions();
    static QString fileDialogFilter();
};

}  // namespace re
