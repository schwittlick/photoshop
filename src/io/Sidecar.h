#pragma once
// The edit state of a raw file, persisted as a JSON sidecar next to it (DSC08912.ARW -> DSC08912.ARW.json).
// The raw is never touched. Values are the EditParams verbatim, in normalised frame coordinates, so a
// sidecar reproduces the edit at any preview or export resolution. Unknown keys are ignored and missing
// keys keep the image's defaults, so older builds can read newer files within the same format version.
#include "core/EditParams.h"
#include <QJsonObject>
#include <QString>

namespace re::sidecar {

constexpr int kVersion = 1;

enum class ReadResult { None, Loaded, Invalid };

QString pathFor(const QString& rawPath);

QJsonObject toJson(const EditParams& p);
// `params` holds the image's defaults on entry and is left untouched when the object is rejected.
bool fromJson(const QJsonObject& o, EditParams* params, QString* error);

// Atomic write next to the raw.
bool write(const QString& rawPath, const EditParams& p, QString* error);
// None when there is no sidecar; Invalid (with `error`) when there is one that cannot be used.
ReadResult read(const QString& rawPath, EditParams* params, QString* error);
// Succeeds when the sidecar is gone afterwards, including when there was none.
bool remove(const QString& rawPath, QString* error);

}  // namespace re::sidecar
