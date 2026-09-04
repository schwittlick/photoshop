#pragma once
// Export: the GPU phase re-runs the identical shader chain at full resolution in tiles
// (working-space linear float), then the CPU phase resizes, colour-manages, sharpens,
// dithers/quantises, encodes and copies EXIF.
#include "core/EditParams.h"
#include "core/OutputProfiles.h"
#include "gpu/RenderBackend.h"
#include <QString>
#include <functional>
#include <vector>

namespace re {

enum class ExportFormat { Tiff16 = 0, Tiff8, Jpeg, Png8, Png16 };
enum class TiffCompression { None = 0, LZW, Deflate };

struct ExportSettings {
    QString outputPath;
    ExportFormat format = ExportFormat::Tiff16;
    TiffCompression tiffCompression = TiffCompression::LZW;
    int jpegQuality = 92;
    icc::Space colourSpace = icc::Space::SRGB;
    bool resize = false;
    int longEdge = 2048;
    Sampler sampler = Sampler::CatmullRom;
    bool copyExif = true;
};

struct ExportJob {
    int width = 0, height = 0;
    std::vector<float> rgb;  // working-space linear, interleaved
};

class Exporter {
public:
    // Return false from the callback to cancel.
    using Progress = std::function<bool(int percent, const QString& stage)>;

    // GPU phase. Must run on the thread that owns the GL context (with it current).
    static bool renderFullRes(RenderBackend& backend, const EditParams& params, Sampler sampler, ExportJob& job,
                              const Progress& progress, QString* error);
    // CPU phase. Safe to run on a worker thread. `warning` receives non-fatal problems (e.g. EXIF copy).
    static bool encode(ExportJob& job, const ExportSettings& s, const EditParams& params, const QString& sourcePath,
                       const Progress& progress, QString* error, QString* warning);

    static QString defaultExtension(ExportFormat f);
    static QString formatName(ExportFormat f);
    static bool isEightBit(ExportFormat f) { return f == ExportFormat::Tiff8 || f == ExportFormat::Jpeg || f == ExportFormat::Png8; }

    // Exposed for tests.
    static void resampleLanczos3(const std::vector<float>& in, int w, int h, int channels, int outW, int outH, std::vector<float>& out);
    static void unsharpMask(std::vector<float>& rgb, int w, int h, float amount, float radius);
};

}  // namespace re
