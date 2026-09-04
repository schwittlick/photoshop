#pragma once
#include <QString>

namespace re {

struct ImageMetadata {
    bool ok = false;
    QString make, model, lensModel, lensMake, dateTime;
    double focalLength = 0, fNumber = 0, focalLength35 = 0, subjectDistance = 0, exposureTime = 0;
    int iso = 0, orientation = 1;
};

// exiv2-based reader for the lens lookup and export passthrough. Never throws.
ImageMetadata readMetadata(const QString& path);

}  // namespace re
