#include "io/Exporter.h"
#include <tiffio.h>
#include <png.h>
#include <exiv2/exiv2.hpp>
#include <QFileInfo>
#include <QDebug>
#include <algorithm>
#include <cmath>
#include <csetjmp>
#include <cstdio>
#include <cstring>
#include <set>
extern "C" {
#include <jpeglib.h>
}

namespace re {

QString Exporter::defaultExtension(ExportFormat f) {
    switch (f) {
        case ExportFormat::Tiff16: case ExportFormat::Tiff8: return QStringLiteral("tif");
        case ExportFormat::Jpeg: return QStringLiteral("jpg");
        case ExportFormat::Png8: case ExportFormat::Png16: return QStringLiteral("png");
    }
    return {};
}

QString Exporter::formatName(ExportFormat f) {
    switch (f) {
        case ExportFormat::Tiff16: return QStringLiteral("TIFF 16-bit");
        case ExportFormat::Tiff8: return QStringLiteral("TIFF 8-bit");
        case ExportFormat::Jpeg: return QStringLiteral("JPEG");
        case ExportFormat::Png8: return QStringLiteral("PNG 8-bit");
        case ExportFormat::Png16: return QStringLiteral("PNG 16-bit");
    }
    return {};
}

// ------------------------------------------------------------------ GPU phase

bool Exporter::renderFullRes(RenderBackend& backend, const EditParams& params, Sampler sampler, ExportJob& job,
                             const Progress& progress, QString* error) {
    if (!backend.hasSource()) { if (error) *error = QStringLiteral("no image loaded"); return false; }
    const QRectF crop = params.geom.cropNorm;
    int outW = std::max(1, int(std::lround(crop.width() * backend.sourceWidth())));
    int outH = std::max(1, int(std::lround(crop.height() * backend.sourceHeight())));
    job.width = outW;
    job.height = outH;
    job.rgb.assign(size_t(outW) * outH * 3, 0.f);

    const int tile = 1024;
    int tilesX = (outW + tile - 1) / tile, tilesY = (outH + tile - 1) / tile, done = 0;
    std::vector<float> rgba;
    RenderOptions opts;
    opts.output = OutputMode::WorkingLinear;
    opts.sampler = sampler;
    opts.floatOutput = true;
    for (int ty = 0; ty < tilesY; ++ty) {
        for (int tx = 0; tx < tilesX; ++tx) {
            int x0 = tx * tile, y0 = ty * tile;
            int tw = std::min(tile, outW - x0), th = std::min(tile, outH - y0);
            ViewSpec view;
            view.outWidth = tw;
            view.outHeight = th;
            view.region = QRectF(double(x0) / outW, double(y0) / outH, double(tw) / outW, double(th) / outH);
            view.crop = crop;
            view.mipLevel = 0;
            TextureHandle tex = backend.render(RenderBackend::SlotAux, view, params, opts);
            if (!tex || !backend.readback(tex, tw, th, rgba)) {
                if (error) *error = QStringLiteral("GPU render failed for tile %1,%2").arg(tx).arg(ty);
                return false;
            }
            for (int y = 0; y < th; ++y) {
                const float* src = &rgba[size_t(y) * tw * 4];
                float* dst = &job.rgb[(size_t(y0 + y) * outW + x0) * 3];
                for (int x = 0; x < tw; ++x) {
                    float a = src[x * 4 + 3];  // regions outside the source frame go black
                    dst[x * 3 + 0] = src[x * 4 + 0] * a;
                    dst[x * 3 + 1] = src[x * 4 + 1] * a;
                    dst[x * 3 + 2] = src[x * 4 + 2] * a;
                }
            }
            ++done;
            if (progress && !progress(done * 100 / (tilesX * tilesY), QStringLiteral("Rendering"))) {
                if (error) *error = QStringLiteral("cancelled");
                return false;
            }
        }
    }
    return true;
}

// ------------------------------------------------------------------ CPU helpers

namespace {

double lanczos3(double x) {
    x = std::abs(x);
    if (x < 1e-9) return 1.0;
    if (x >= 3.0) return 0.0;
    double px = kPi * x;
    return 3.0 * std::sin(px) * std::sin(px / 3.0) / (px * px);
}

struct Weights1D {
    std::vector<int> start;
    std::vector<int> count;
    std::vector<float> w;  // concatenated
};

Weights1D buildWeights(int inN, int outN) {
    Weights1D W;
    W.start.resize(outN);
    W.count.resize(outN);
    double scale = double(inN) / outN;
    double support = 3.0 * std::max(1.0, scale);
    for (int o = 0; o < outN; ++o) {
        double centre = (o + 0.5) * scale - 0.5;
        int lo = std::max(0, int(std::floor(centre - support)));
        int hi = std::min(inN - 1, int(std::ceil(centre + support)));
        W.start[o] = lo;
        W.count[o] = hi - lo + 1;
        double sum = 0;
        size_t base = W.w.size();
        for (int i = lo; i <= hi; ++i) {
            double wv = lanczos3((i - centre) / std::max(1.0, scale));
            W.w.push_back(float(wv));
            sum += wv;
        }
        if (sum > 0) for (int i = 0; i < W.count[o]; ++i) W.w[base + i] = float(W.w[base + i] / sum);
    }
    return W;
}

}  // namespace

void Exporter::resampleLanczos3(const std::vector<float>& in, int w, int h, int ch, int outW, int outH, std::vector<float>& out) {
    // horizontal pass
    Weights1D wx = buildWeights(w, outW);
    std::vector<float> tmp(size_t(outW) * h * ch, 0.f);
    for (int y = 0; y < h; ++y) {
        const float* row = &in[size_t(y) * w * ch];
        float* dst = &tmp[size_t(y) * outW * ch];
        size_t wi = 0;
        for (int o = 0; o < outW; ++o) {
            int s = wx.start[o], n = wx.count[o];
            for (int c = 0; c < ch; ++c) {
                float acc = 0;
                for (int i = 0; i < n; ++i) acc += row[(s + i) * ch + c] * wx.w[wi + i];
                dst[o * ch + c] = acc;
            }
            wi += n;
        }
    }
    // vertical pass
    Weights1D wy = buildWeights(h, outH);
    out.assign(size_t(outW) * outH * ch, 0.f);
    size_t wi = 0;
    for (int o = 0; o < outH; ++o) {
        int s = wy.start[o], n = wy.count[o];
        float* dst = &out[size_t(o) * outW * ch];
        for (int i = 0; i < n; ++i) {
            float wv = wy.w[wi + i];
            const float* src = &tmp[size_t(s + i) * outW * ch];
            for (size_t k = 0; k < size_t(outW) * ch; ++k) dst[k] += src[k] * wv;
        }
        wi += n;
    }
}

void Exporter::unsharpMask(std::vector<float>& rgb, int w, int h, float amount, float radius) {
    if (amount <= 0 || radius <= 0) return;
    float sigma = radius;
    int r = std::max(1, int(std::ceil(sigma * 3)));
    std::vector<float> k(2 * r + 1);
    float ks = 0;
    for (int i = -r; i <= r; ++i) { k[i + r] = std::exp(-0.5f * i * i / (sigma * sigma)); ks += k[i + r]; }
    for (float& v : k) v /= ks;
    std::vector<float> tmp(rgb.size()), blur(rgb.size());
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            for (int c = 0; c < 3; ++c) {
                float acc = 0;
                for (int i = -r; i <= r; ++i) acc += rgb[(size_t(y) * w + std::clamp(x + i, 0, w - 1)) * 3 + c] * k[i + r];
                tmp[(size_t(y) * w + x) * 3 + c] = acc;
            }
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            for (int c = 0; c < 3; ++c) {
                float acc = 0;
                for (int i = -r; i <= r; ++i) acc += tmp[(size_t(std::clamp(y + i, 0, h - 1)) * w + x) * 3 + c] * k[i + r];
                blur[(size_t(y) * w + x) * 3 + c] = acc;
            }
    float strength = amount / 100.f * 1.5f;
    for (size_t i = 0; i < rgb.size(); ++i) rgb[i] = std::clamp(rgb[i] + strength * (rgb[i] - blur[i]), 0.f, 1.f);
}

// ------------------------------------------------------------------ encoders

namespace {

struct JpegErr {
    jpeg_error_mgr pub;
    jmp_buf jb;
    char msg[JMSG_LENGTH_MAX];
};

void jpegErrorExit(j_common_ptr cinfo) {
    JpegErr* e = reinterpret_cast<JpegErr*>(cinfo->err);
    (*cinfo->err->format_message)(cinfo, e->msg);
    longjmp(e->jb, 1);
}

bool writeJpeg(const QString& path, int w, int h, const std::vector<uint8_t>& rgb, int quality,
               const std::vector<uint8_t>& icc, QString* error) {
    FILE* f = std::fopen(path.toLocal8Bit().constData(), "wb");
    if (!f) { *error = QStringLiteral("cannot open %1 for writing").arg(path); return false; }
    jpeg_compress_struct cinfo;
    JpegErr jerr;
    cinfo.err = jpeg_std_error(&jerr.pub);
    jerr.pub.error_exit = jpegErrorExit;
    if (setjmp(jerr.jb)) {
        *error = QStringLiteral("libjpeg: %1").arg(jerr.msg);
        jpeg_destroy_compress(&cinfo);
        std::fclose(f);
        return false;
    }
    jpeg_create_compress(&cinfo);
    jpeg_stdio_dest(&cinfo, f);
    cinfo.image_width = w;
    cinfo.image_height = h;
    cinfo.input_components = 3;
    cinfo.in_color_space = JCS_RGB;
    jpeg_set_defaults(&cinfo);
    jpeg_set_quality(&cinfo, std::clamp(quality, 1, 100), TRUE);
    for (int i = 0; i < 3; ++i) { cinfo.comp_info[i].h_samp_factor = 1; cinfo.comp_info[i].v_samp_factor = 1; }  // 4:4:4
    cinfo.optimize_coding = TRUE;
    jpeg_start_compress(&cinfo, TRUE);
    if (!icc.empty()) {
        const size_t maxChunk = 65533 - 14;
        int total = int((icc.size() + maxChunk - 1) / maxChunk);
        for (int seq = 0; seq < total; ++seq) {
            size_t off = seq * maxChunk, len = std::min(maxChunk, icc.size() - off);
            std::vector<uint8_t> marker(14 + len);
            std::memcpy(marker.data(), "ICC_PROFILE\0", 12);
            marker[12] = uint8_t(seq + 1);
            marker[13] = uint8_t(total);
            std::memcpy(marker.data() + 14, icc.data() + off, len);
            jpeg_write_marker(&cinfo, JPEG_APP0 + 2, marker.data(), unsigned(marker.size()));
        }
    }
    while (cinfo.next_scanline < cinfo.image_height) {
        JSAMPROW row = const_cast<JSAMPROW>(&rgb[size_t(cinfo.next_scanline) * w * 3]);
        jpeg_write_scanlines(&cinfo, &row, 1);
    }
    jpeg_finish_compress(&cinfo);
    jpeg_destroy_compress(&cinfo);
    std::fclose(f);
    return true;
}

bool writeTiff(const QString& path, int w, int h, int bits, const void* data, TiffCompression comp,
               const std::vector<uint8_t>& icc, QString* error) {
    TIFF* tif = TIFFOpen(path.toLocal8Bit().constData(), "w");
    if (!tif) { *error = QStringLiteral("cannot open %1 for writing").arg(path); return false; }
    TIFFSetField(tif, TIFFTAG_IMAGEWIDTH, uint32_t(w));
    TIFFSetField(tif, TIFFTAG_IMAGELENGTH, uint32_t(h));
    TIFFSetField(tif, TIFFTAG_SAMPLESPERPIXEL, 3);
    TIFFSetField(tif, TIFFTAG_BITSPERSAMPLE, bits);
    TIFFSetField(tif, TIFFTAG_ORIENTATION, ORIENTATION_TOPLEFT);
    TIFFSetField(tif, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
    TIFFSetField(tif, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_RGB);
    TIFFSetField(tif, TIFFTAG_SOFTWARE, "rawedit");
    switch (comp) {
        case TiffCompression::None: TIFFSetField(tif, TIFFTAG_COMPRESSION, COMPRESSION_NONE); break;
        case TiffCompression::LZW: TIFFSetField(tif, TIFFTAG_COMPRESSION, COMPRESSION_LZW); TIFFSetField(tif, TIFFTAG_PREDICTOR, 2); break;
        case TiffCompression::Deflate: TIFFSetField(tif, TIFFTAG_COMPRESSION, COMPRESSION_ADOBE_DEFLATE); TIFFSetField(tif, TIFFTAG_PREDICTOR, 2); break;
    }
    size_t rowBytesForStrip = size_t(w) * 3 * (bits / 8);
    TIFFSetField(tif, TIFFTAG_ROWSPERSTRIP, uint32_t(std::clamp<size_t>((1u << 20) / std::max<size_t>(rowBytesForStrip, 1), 1, size_t(h))));
    if (!icc.empty()) TIFFSetField(tif, TIFFTAG_ICCPROFILE, uint32_t(icc.size()), icc.data());
    size_t rowBytes = size_t(w) * 3 * (bits / 8);
    const uint8_t* base = static_cast<const uint8_t*>(data);
    for (int y = 0; y < h; ++y) {
        if (TIFFWriteScanline(tif, const_cast<uint8_t*>(base + size_t(y) * rowBytes), uint32_t(y), 0) < 0) {
            *error = QStringLiteral("libtiff failed writing row %1").arg(y);
            TIFFClose(tif);
            return false;
        }
    }
    TIFFClose(tif);
    return true;
}

bool writePng(const QString& path, int w, int h, int bits, const void* data, const std::vector<uint8_t>& icc, QString* error) {
    FILE* f = std::fopen(path.toLocal8Bit().constData(), "wb");
    if (!f) { *error = QStringLiteral("cannot open %1 for writing").arg(path); return false; }
    png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    png_infop info = png ? png_create_info_struct(png) : nullptr;
    if (!png || !info) {
        *error = QStringLiteral("libpng initialisation failed");
        if (png) png_destroy_write_struct(&png, info ? &info : nullptr);
        std::fclose(f);
        return false;
    }
    if (setjmp(png_jmpbuf(png))) {
        *error = QStringLiteral("libpng failed writing %1").arg(path);
        png_destroy_write_struct(&png, &info);
        std::fclose(f);
        return false;
    }
    png_init_io(png, f);
    png_set_IHDR(png, info, w, h, bits, PNG_COLOR_TYPE_RGB, PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    if (!icc.empty()) png_set_iCCP(png, info, "ICC Profile", PNG_COMPRESSION_TYPE_BASE, icc.data(), png_uint_32(icc.size()));
    png_write_info(png, info);
    if (bits == 16) png_set_swap(png);  // host is little-endian; PNG wants big-endian
    size_t rowBytes = size_t(w) * 3 * (bits / 8);
    const uint8_t* base = static_cast<const uint8_t*>(data);
    for (int y = 0; y < h; ++y) png_write_row(png, const_cast<png_bytep>(base + size_t(y) * rowBytes));
    png_write_end(png, nullptr);
    png_destroy_write_struct(&png, &info);
    std::fclose(f);
    return true;
}

// EXIF passthrough: everything except structural TIFF tags, embedded previews and
// geometry-invalidated fields. Returns false with a message on failure (non-fatal).
bool copyExif(const QString& sourcePath, const QString& outPath, int w, int h, icc::Space space, bool keepMakerNote, QString* msg) {
    try {
        auto src = Exiv2::ImageFactory::open(sourcePath.toStdString());
        src->readMetadata();
        Exiv2::ExifData ed = src->exifData();
        static const std::set<std::string> dropKeys = {
            "Exif.Image.ImageWidth", "Exif.Image.ImageLength", "Exif.Image.BitsPerSample", "Exif.Image.Compression",
            "Exif.Image.PhotometricInterpretation", "Exif.Image.StripOffsets", "Exif.Image.SamplesPerPixel",
            "Exif.Image.RowsPerStrip", "Exif.Image.StripByteCounts", "Exif.Image.PlanarConfiguration",
            "Exif.Image.SubIFDs", "Exif.Image.TileWidth", "Exif.Image.TileLength", "Exif.Image.TileOffsets",
            "Exif.Image.TileByteCounts", "Exif.Image.JPEGInterchangeFormat", "Exif.Image.JPEGInterchangeFormatLength",
            "Exif.Image.DNGVersion", "Exif.Image.DNGBackwardVersion", "Exif.Image.DNGPrivateData", "Exif.Image.NewSubfileType",
            "Exif.Image.CFARepeatPatternDim", "Exif.Image.CFAPattern", "Exif.Image.Orientation", "Exif.Image.XMLPacket",
            "Exif.Image.InterColorProfile", "Exif.Image.PrintImageMatching", "Exif.Photo.PixelXDimension",
            "Exif.Photo.PixelYDimension", "Exif.Photo.ColorSpace", "Exif.Image.Software"};
        for (auto it = ed.begin(); it != ed.end();) {
            std::string g = it->groupName();
            bool dngTag = (g == "Image" && it->tag() >= 0xC612);  // DNG-specific tags (profiles, matrices, black levels)
            bool drop = dngTag || dropKeys.count(it->key()) > 0 || g == "Thumbnail" || g.rfind("SubImage", 0) == 0 || g == "Image2"
                     || g == "Image3" || g == "SubThumb1" || (!keepMakerNote && (it->key() == "Exif.Photo.MakerNote" || (g != "Image" && g != "Photo" && g != "Iop" && g != "GPSInfo")));
            if (drop) it = ed.erase(it);
            else ++it;
        }
        ed["Exif.Image.Orientation"] = uint16_t(1);
        ed["Exif.Image.ImageWidth"] = uint32_t(w);
        ed["Exif.Image.ImageLength"] = uint32_t(h);
        ed["Exif.Photo.PixelXDimension"] = uint32_t(w);
        ed["Exif.Photo.PixelYDimension"] = uint32_t(h);
        ed["Exif.Photo.ColorSpace"] = uint16_t(space == icc::Space::SRGB ? 1 : 0xFFFF);
        ed["Exif.Image.Software"] = "rawedit";
        auto dst = Exiv2::ImageFactory::open(outPath.toStdString());
        dst->readMetadata();
        dst->setExifData(ed);
        dst->writeMetadata();
        return true;
    } catch (const std::exception& e) {
        if (msg) *msg = QString::fromUtf8(e.what());
        return false;
    }
}

uint32_t xorshift(uint32_t& s) {
    s ^= s << 13; s ^= s >> 17; s ^= s << 5;
    return s;
}

}  // namespace

// ------------------------------------------------------------------ CPU phase

bool Exporter::encode(ExportJob& job, const ExportSettings& s, const EditParams& params, const QString& sourcePath,
                      const Progress& progress, QString* error, QString* warning) {
    auto report = [&](int pct, const char* stage) { return !progress || progress(pct, QString::fromUtf8(stage)); };
    if (job.width <= 0 || job.height <= 0) { if (error) *error = QStringLiteral("empty render"); return false; }
    int w = job.width, h = job.height;

    // 1. resize (linear light)
    if (s.resize && s.longEdge > 0) {
        double scale = double(s.longEdge) / std::max(w, h);
        int nw = std::max(1, int(std::lround(w * scale))), nh = std::max(1, int(std::lround(h * scale)));
        if (nw != w || nh != h) {
            if (!report(5, "Resizing")) return false;
            std::vector<float> out;
            resampleLanczos3(job.rgb, w, h, 3, nw, nh, out);
            job.rgb.swap(out);
            w = nw; h = nh;
            for (float& v : job.rgb) v = std::max(v, 0.f);
        }
    }
    if (!report(25, "Colour transform")) return false;

    // 2. colour transform to the output profile (encoded floats 0..1)
    icc::Profile outProfile = icc::createProfile(s.colourSpace);
    if (!outProfile) { if (error) *error = QStringLiteral("could not build the output profile"); return false; }
    std::vector<float> enc(job.rgb.size());
    {
        icc::Transform xf(outProfile.get());
        if (!xf.valid()) { if (error) *error = QStringLiteral("could not build the colour transform"); return false; }
        xf.apply(job.rgb.data(), enc.data(), size_t(w) * h);
    }
    std::vector<float>().swap(job.rgb);
    for (float& v : enc) v = std::clamp(v, 0.f, 1.f);

    // 3. output sharpening, last, on the encoded image
    if (params.outputSharpenAmount > 0) {
        if (!report(45, "Sharpening")) return false;
        unsharpMask(enc, w, h, params.outputSharpenAmount, params.outputSharpenRadius);
    }
    if (!report(60, "Encoding")) return false;

    // 4. quantise
    std::vector<uint8_t> icc = icc::profileBytes(outProfile.get());
    bool ok = false;
    QString err;
    if (isEightBit(s.format)) {
        std::vector<uint8_t> px(size_t(w) * h * 3);
        uint32_t seed = 0x9E3779B9u;
        for (size_t i = 0; i < px.size(); ++i) {
            // triangular-PDF dither, +-1 LSB, before rounding
            float d = (float(xorshift(seed) & 0xFFFF) + float(xorshift(seed) & 0xFFFF)) / 65536.f - 1.f;
            px[i] = uint8_t(std::clamp(int(std::lround(enc[i] * 255.f + d)), 0, 255));
        }
        switch (s.format) {
            case ExportFormat::Tiff8: ok = writeTiff(s.outputPath, w, h, 8, px.data(), s.tiffCompression, icc, &err); break;
            case ExportFormat::Jpeg: ok = writeJpeg(s.outputPath, w, h, px, s.jpegQuality, icc, &err); break;
            case ExportFormat::Png8: ok = writePng(s.outputPath, w, h, 8, px.data(), icc, &err); break;
            default: break;
        }
    } else {
        std::vector<uint16_t> px(size_t(w) * h * 3);
        for (size_t i = 0; i < px.size(); ++i) px[i] = uint16_t(std::lround(enc[i] * 65535.f));
        switch (s.format) {
            case ExportFormat::Tiff16: ok = writeTiff(s.outputPath, w, h, 16, px.data(), s.tiffCompression, icc, &err); break;
            case ExportFormat::Png16: ok = writePng(s.outputPath, w, h, 16, px.data(), icc, &err); break;
            default: break;
        }
    }
    if (!ok) { if (error) *error = err; return false; }
    if (!report(90, "Writing metadata")) return false;

    // 5. EXIF passthrough (non-fatal)
    if (s.copyExif && !sourcePath.isEmpty()) {
        QString msg;
        if (!copyExif(sourcePath, s.outputPath, w, h, s.colourSpace, true, &msg)) {
            QString msg2;
            if (!copyExif(sourcePath, s.outputPath, w, h, s.colourSpace, false, &msg2) && warning)
                *warning = QStringLiteral("EXIF metadata could not be copied: %1").arg(msg2.isEmpty() ? msg : msg2);
        }
    }
    report(100, "Done");
    return true;
}

}  // namespace re
