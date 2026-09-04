#pragma once
#include "io/RawImage.h"
#include <QString>
#include <QStringList>
#include <memory>

namespace re {

class RawLoader {
public:
    // Blocking decode; run on a worker thread. Returns null and fills *error on failure.
    static std::shared_ptr<RawImage> load(const QString& path, QString* error);
    static QStringList supportedExtensions();
    static QString fileDialogFilter();
};

}  // namespace re
