#include "io/MetadataReader.h"
#include <exiv2/exiv2.hpp>
#include <QDebug>

namespace re {

ImageMetadata readMetadata(const QString& path) {
    ImageMetadata m;
    try {
        auto image = Exiv2::ImageFactory::open(path.toStdString());
        if (!image) return m;
        image->readMetadata();
        const Exiv2::ExifData& ed = image->exifData();
        if (ed.empty()) return m;
        auto str = [&](Exiv2::ExifData::const_iterator it) {
            return it != ed.end() ? QString::fromStdString(it->print(&ed)).trimmed() : QString();
        };
        auto num = [&](Exiv2::ExifData::const_iterator it) { return it != ed.end() && it->count() ? double(it->toFloat()) : 0.0; };
        auto find = [&](const char* key) { return ed.findKey(Exiv2::ExifKey(key)); };

        m.make = str(Exiv2::make(ed));
        m.model = str(Exiv2::model(ed));
        m.lensModel = str(Exiv2::lensName(ed));
        if (m.lensModel.isEmpty()) m.lensModel = str(find("Exif.Photo.LensModel"));
        m.lensMake = str(find("Exif.Photo.LensMake"));
        m.focalLength = num(Exiv2::focalLength(ed));
        m.fNumber = num(Exiv2::fNumber(ed));
        m.focalLength35 = num(find("Exif.Photo.FocalLengthIn35mmFilm"));
        m.subjectDistance = num(Exiv2::subjectDistance(ed));
        m.exposureTime = num(Exiv2::exposureTime(ed));
        auto isoIt = Exiv2::isoSpeed(ed);
        if (isoIt != ed.end() && isoIt->count()) m.iso = int(isoIt->toInt64());
        auto orIt = Exiv2::orientation(ed);
        if (orIt != ed.end() && orIt->count()) m.orientation = int(orIt->toInt64());
        m.dateTime = str(Exiv2::dateTimeOriginal(ed));
        m.ok = true;
    } catch (const std::exception& e) {
        qWarning() << "exiv2:" << e.what();
    }
    return m;
}

}  // namespace re
