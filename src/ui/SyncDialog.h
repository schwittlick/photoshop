#pragma once
// "Sync settings": choose which groups of the active image's adjustments are copied to every
// other loaded image. The choice is remembered between runs.
#include "core/EditParams.h"
#include <QDialog>

class QCheckBox;

namespace re {

class SyncDialog : public QDialog {
    Q_OBJECT
public:
    SyncDialog(const QString& sourceName, int targetCount, QWidget* parent = nullptr);
    SyncMask mask() const;

    static SyncMask lastMask();                 // remembered choice, or the defaults
    static void saveMask(const SyncMask& m);
    static QString describe(const SyncMask& m); // "tone, curves, lens, output sharpening"

protected:
    void accept() override;

private:
    QCheckBox* wb_;
    QCheckBox* tone_;
    QCheckBox* curves_;
    QCheckBox* lens_;
    QCheckBox* rotation_;
    QCheckBox* perspective_;
    QCheckBox* crop_;
    QCheckBox* sharpen_;
};

}  // namespace re
