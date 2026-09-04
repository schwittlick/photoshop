#include "io/RawLoader.h"
#include <libraw/libraw.h>
#include <QFileInfo>
#include <algorithm>
#include <cstring>

namespace re {

Vec3 RawImage::sampleRaw(int x, int y, int radius) const {
    Vec3 acc;
    int count = 0;
    for (int dy = -radius; dy <= radius; ++dy)
        for (int dx = -radius; dx <= radius; ++dx) {
            int px = x + dx, py = y + dy;
            if (px < 0 || py < 0 || px >= width || py >= height) continue;
            const uint16_t* p = &rgb[(size_t(py) * width + px) * 3];
            acc.x += p[0]; acc.y += p[1]; acc.z += p[2];
            ++count;
        }
    if (!count) return {1, 1, 1};
    acc = acc / (65535.0 * count);
    return {acc.x / std::max(preMul[0], 1e-4f), acc.y / std::max(preMul[1], 1e-4f), acc.z / std::max(preMul[2], 1e-4f)};
}

QStringList RawLoader::supportedExtensions() {
    return {"cr2", "cr3", "nef", "nrw", "arw", "srf", "sr2", "raf", "dng", "orf", "rw2", "pef", "raw", "rwl", "3fr",
            "fff", "iiq", "mrw", "mos", "kdc", "dcr", "erf", "mef", "x3f", "srw"};
}

QString RawLoader::fileDialogFilter() {
    QStringList pats;
    for (const QString& e : supportedExtensions()) pats << "*." + e << "*." + e.toUpper();
    return QStringLiteral("Raw images (%1);;All files (*)").arg(pats.join(' '));
}

std::shared_ptr<RawImage> RawLoader::load(const QString& path, QString* error) {
    auto fail = [&](const QString& msg) { if (error) *error = msg; return std::shared_ptr<RawImage>(); };
    LibRaw proc;
    auto& P = proc.imgdata.params;
    P.output_bps = 16;
    P.output_color = 0;      // keep camera-native colour; we apply the matrix on the GPU
    P.no_auto_bright = 1;
    P.gamm[0] = P.gamm[1] = 1.0;  // linear
    P.use_camera_wb = 1;     // as-shot multipliers before demosaic (better interpolation)
    P.highlight = 1;         // "unclip": keep every channel's data, we handle blown pixels ourselves
    P.user_qual = 3;         // AHD
    P.half_size = 0;
    P.use_camera_matrix = 1;

    int rc = proc.open_file(path.toLocal8Bit().constData());
    if (rc != LIBRAW_SUCCESS) return fail(QStringLiteral("LibRaw: %1").arg(libraw_strerror(rc)));

    const auto& idata = proc.imgdata.idata;
    auto img = std::make_shared<RawImage>();
    img->camera = colour::CameraColour::fromLibRaw(proc.imgdata.color.cam_xyz, proc.imgdata.color.rgb_cam, idata.colors);
    if (idata.colors != 3 || !img->camera.valid) {
        // Unusual sensor layout or no matrix: let LibRaw produce linear sRGB and treat that as the camera space.
        P.output_color = 1;
        img->colourIsSRGBFallback = true;
        img->camera = colour::CameraColour::fallbackSRGB();
    }

    rc = proc.unpack();
    if (rc != LIBRAW_SUCCESS) return fail(QStringLiteral("LibRaw unpack: %1").arg(libraw_strerror(rc)));
    rc = proc.dcraw_process();
    if (rc != LIBRAW_SUCCESS) return fail(QStringLiteral("LibRaw process: %1").arg(libraw_strerror(rc)));

    int errc = 0;
    libraw_processed_image_t* out = proc.dcraw_make_mem_image(&errc);
    if (!out) return fail(QStringLiteral("LibRaw image: %1").arg(libraw_strerror(errc)));
    if (out->type != LIBRAW_IMAGE_BITMAP || out->colors != 3 || out->bits != 16) {
        LibRaw::dcraw_clear_mem(out);
        return fail(QStringLiteral("LibRaw produced an unexpected image format (%1 colours, %2 bits)").arg(out->colors).arg(out->bits));
    }
    img->width = out->width;
    img->height = out->height;
    img->rgb.resize(size_t(out->width) * out->height * 3);
    std::memcpy(img->rgb.data(), out->data, std::min<size_t>(out->data_size, img->rgb.size() * 2));
    LibRaw::dcraw_clear_mem(out);

    const auto& C = proc.imgdata.color;
    for (int c = 0; c < 4; ++c) {
        img->preMul[c] = C.pre_mul[c] > 0 ? C.pre_mul[c] : 1.f;
        img->camMul[c] = C.cam_mul[c];
    }
    if (img->colourIsSRGBFallback) {
        // LibRaw's sRGB conversion already undid its own scaling asymmetry; the data is white balanced.
        img->preMul[0] = img->preMul[1] = img->preMul[2] = img->preMul[3] = 1.f;
    }
    img->black = C.black;
    img->maximum = C.maximum;
    img->flip = proc.imgdata.sizes.flip;
    img->make = QString::fromUtf8(idata.make);
    img->model = QString::fromUtf8(idata.model);
    img->lens = QString::fromUtf8(proc.imgdata.lens.Lens);
    img->focal = proc.imgdata.other.focal_len;
    img->aperture = proc.imgdata.other.aperture;
    img->shutter = proc.imgdata.other.shutter;
    img->iso = int(proc.imgdata.other.iso_speed);
    img->focal35 = proc.imgdata.lens.FocalLengthIn35mmFormat;
    return img;
}

}  // namespace re
