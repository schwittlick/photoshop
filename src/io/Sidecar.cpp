#include "io/Sidecar.h"
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <algorithm>
#include <cmath>

namespace re::sidecar {

namespace {

const QLatin1String kFormat("photoshop-edit");

QJsonArray curveJson(const CurvePoints& c) {
    QJsonArray a;
    for (const QPointF& p : c.pts) a.append(QJsonArray{p.x(), p.y()});
    return a;
}

QJsonObject child(const QJsonObject& o, const char* key) { return o.value(QLatin1String(key)).toObject(); }

// The shortest decimal that reads back as exactly the same float, so the file says 0.06 rather than
// 0.05999999865889549 and still round-trips bit for bit.
double tidy(float f) {
    for (int digits = 6; digits <= 9; ++digits) {
        const double d = QString::number(double(f), 'g', digits).toDouble();
        if (float(d) == f) return d;
    }
    return double(f);
}

float numf(const QJsonObject& o, const char* key, float fallback) {
    const QJsonValue v = o.value(QLatin1String(key));
    if (!v.isDouble()) return fallback;
    const double d = v.toDouble();
    return std::isfinite(d) ? float(d) : fallback;
}

bool flag(const QJsonObject& o, const char* key, bool fallback) {
    const QJsonValue v = o.value(QLatin1String(key));
    return v.isBool() ? v.toBool() : fallback;
}

// A [x, y] pair of finite numbers.
bool readPair(const QJsonValue& v, double* x, double* y) {
    if (!v.isArray()) return false;
    const QJsonArray a = v.toArray();
    if (a.size() != 2 || !a[0].isDouble() || !a[1].isDouble()) return false;
    *x = a[0].toDouble();
    *y = a[1].toDouble();
    return std::isfinite(*x) && std::isfinite(*y);
}

// Absent keeps the current curve; present must be >= 2 points in [0,1]^2 with non-decreasing x.
bool readCurve(const QJsonValue& v, CurvePoints* out) {
    if (v.isUndefined() || v.isNull()) return true;
    if (!v.isArray()) return false;
    std::vector<QPointF> pts;
    for (const QJsonValue& e : v.toArray()) {
        double x, y;
        if (!readPair(e, &x, &y) || x < 0 || x > 1 || y < 0 || y > 1) return false;
        if (!pts.empty() && x < pts.back().x()) return false;
        pts.emplace_back(x, y);
    }
    if (pts.size() < 2) return false;
    out->pts = std::move(pts);
    return true;
}

}  // namespace

QString pathFor(const QString& rawPath) { return rawPath + QStringLiteral(".json"); }

QJsonObject toJson(const EditParams& p) {
    const auto& t = p.tone;
    const auto& l = p.lens;
    const auto& g = p.geom;
    QJsonObject tone{{"exposure", tidy(t.exposureEV)}, {"contrast", tidy(t.contrast)}, {"highlights", tidy(t.highlights)},
                     {"shadows", tidy(t.shadows)},     {"whites", tidy(t.whites)},     {"blacks", tidy(t.blacks)},
                     {"saturation", tidy(t.saturation)}, {"vibrance", tidy(t.vibrance)},
                     {"parametric", QJsonObject{{"highlights", tidy(t.pHighlights)}, {"lights", tidy(t.pLights)},
                                                {"darks", tidy(t.pDarks)}, {"shadows", tidy(t.pShadows)}}},
                     {"curves", QJsonObject{{"master", curveJson(t.curveMaster)}, {"red", curveJson(t.curveR)},
                                            {"green", curveJson(t.curveG)}, {"blue", curveJson(t.curveB)}}}};
    QJsonObject lens{{"profile", l.lensAuto}, {"profileDistortion", tidy(l.profileDistortion)}, {"profileVignetting", tidy(l.profileVignetting)},
                     {"profileCA", l.profileCA},
                     {"distortion", QJsonObject{{"a", tidy(l.distA)}, {"b", tidy(l.distB)}, {"c", tidy(l.distC)}}},
                     {"ca", QJsonObject{{"red", tidy(l.caRed)}, {"blue", tidy(l.caBlue)}}},
                     {"vignette", QJsonObject{{"amount", tidy(l.vignetteAmount)}, {"midpoint", tidy(l.vignetteMidpoint)}}}};
    QJsonArray corners;
    for (const QVector2D& c : g.corners) corners.append(QJsonArray{tidy(c.x()), tidy(c.y())});
    QJsonArray guides;
    for (const Guide& gd : g.guides)
        guides.append(QJsonObject{{"from", QJsonArray{gd.a.x(), gd.a.y()}}, {"to", QJsonArray{gd.b.x(), gd.b.y()}}, {"vertical", gd.vertical}});
    QJsonObject geom{{"rotation", tidy(g.rotationDeg)},
                     {"perspective", QJsonObject{{"vertical", tidy(g.perspVertical)}, {"horizontal", tidy(g.perspHorizontal)}, {"corners", corners}}},
                     {"guides", guides},
                     {"crop", QJsonArray{g.cropNorm.x(), g.cropNorm.y(), g.cropNorm.width(), g.cropNorm.height()}}};
    QJsonObject output{{"sharpenAmount", tidy(p.outputSharpenAmount)}, {"sharpenRadius", tidy(p.outputSharpenRadius)}};
    return QJsonObject{{"format", kFormat}, {"version", kVersion},
                       {"wb", QJsonObject{{"temp", tidy(p.wb.temp)}, {"tint", tidy(p.wb.tint)}}},
                       {"tone", tone}, {"lens", lens}, {"geom", geom}, {"output", output}};
}

bool fromJson(const QJsonObject& o, EditParams* params, QString* error) {
    auto fail = [&](const QString& m) { if (error) *error = m; return false; };
    if (o.value(QLatin1String("format")).toString() != kFormat) return fail(QStringLiteral("not a photoshop edit file"));
    const int version = o.value(QLatin1String("version")).toInt(0);
    if (version < 1) return fail(QStringLiteral("missing format version"));
    if (version > kVersion) return fail(QStringLiteral("written by a newer version (format %1, this build reads up to %2)").arg(version).arg(kVersion));

    EditParams p = *params;
    const QJsonObject wb = child(o, "wb");
    p.wb.temp = numf(wb, "temp", p.wb.temp);
    p.wb.tint = numf(wb, "tint", p.wb.tint);

    const QJsonObject tone = child(o, "tone");
    auto& t = p.tone;
    t.exposureEV = numf(tone, "exposure", t.exposureEV);
    t.contrast = numf(tone, "contrast", t.contrast);
    t.highlights = numf(tone, "highlights", t.highlights);
    t.shadows = numf(tone, "shadows", t.shadows);
    t.whites = numf(tone, "whites", t.whites);
    t.blacks = numf(tone, "blacks", t.blacks);
    t.saturation = numf(tone, "saturation", t.saturation);
    t.vibrance = numf(tone, "vibrance", t.vibrance);
    const QJsonObject para = child(tone, "parametric");
    t.pHighlights = numf(para, "highlights", t.pHighlights);
    t.pLights = numf(para, "lights", t.pLights);
    t.pDarks = numf(para, "darks", t.pDarks);
    t.pShadows = numf(para, "shadows", t.pShadows);
    const QJsonObject curves = child(tone, "curves");
    if (!readCurve(curves.value(QLatin1String("master")), &t.curveMaster) || !readCurve(curves.value(QLatin1String("red")), &t.curveR) ||
        !readCurve(curves.value(QLatin1String("green")), &t.curveG) || !readCurve(curves.value(QLatin1String("blue")), &t.curveB))
        return fail(QStringLiteral("malformed curve"));

    const QJsonObject lens = child(o, "lens");
    auto& l = p.lens;
    l.lensAuto = flag(lens, "profile", l.lensAuto);
    l.profileDistortion = numf(lens, "profileDistortion", l.profileDistortion);
    l.profileVignetting = numf(lens, "profileVignetting", l.profileVignetting);
    l.profileCA = flag(lens, "profileCA", l.profileCA);
    const QJsonObject dist = child(lens, "distortion");
    l.distA = numf(dist, "a", l.distA);
    l.distB = numf(dist, "b", l.distB);
    l.distC = numf(dist, "c", l.distC);
    const QJsonObject ca = child(lens, "ca");
    l.caRed = numf(ca, "red", l.caRed);
    l.caBlue = numf(ca, "blue", l.caBlue);
    const QJsonObject vig = child(lens, "vignette");
    l.vignetteAmount = numf(vig, "amount", l.vignetteAmount);
    l.vignetteMidpoint = numf(vig, "midpoint", l.vignetteMidpoint);

    const QJsonObject geom = child(o, "geom");
    auto& g = p.geom;
    g.rotationDeg = numf(geom, "rotation", g.rotationDeg);
    const QJsonObject persp = child(geom, "perspective");
    g.perspVertical = numf(persp, "vertical", g.perspVertical);
    g.perspHorizontal = numf(persp, "horizontal", g.perspHorizontal);
    const QJsonValue corners = persp.value(QLatin1String("corners"));
    if (corners.isArray()) {
        const QJsonArray a = corners.toArray();
        if (a.size() != 4) return fail(QStringLiteral("malformed perspective corners"));
        for (int i = 0; i < 4; ++i) {
            double x, y;
            if (!readPair(a[i], &x, &y)) return fail(QStringLiteral("malformed perspective corners"));
            g.corners[size_t(i)] = QVector2D(float(x), float(y));
        }
    }
    const QJsonValue guides = geom.value(QLatin1String("guides"));
    if (guides.isArray()) {
        std::vector<Guide> list;
        for (const QJsonValue& v : guides.toArray()) {
            if (!v.isObject()) return fail(QStringLiteral("malformed guides"));
            const QJsonObject o2 = v.toObject();
            Guide gd;
            double ax, ay, bx, by;
            if (!readPair(o2.value(QLatin1String("from")), &ax, &ay) || !readPair(o2.value(QLatin1String("to")), &bx, &by)) return fail(QStringLiteral("malformed guides"));
            gd.a = QPointF(ax, ay);
            gd.b = QPointF(bx, by);
            gd.vertical = flag(o2, "vertical", true);
            if (list.size() < 4) list.push_back(gd);
        }
        g.guides = std::move(list);
    }
    const QJsonValue crop = geom.value(QLatin1String("crop"));
    if (crop.isArray()) {
        const QJsonArray a = crop.toArray();
        if (a.size() != 4) return fail(QStringLiteral("malformed crop"));
        double v[4];
        for (int i = 0; i < 4; ++i) {
            if (!a[i].isDouble() || !std::isfinite(a[i].toDouble())) return fail(QStringLiteral("malformed crop"));
            v[i] = a[i].toDouble();
        }
        QRectF r = QRectF(v[0], v[1], v[2], v[3]) & QRectF(0, 0, 1, 1);
        if (r.isEmpty()) return fail(QStringLiteral("crop outside the frame"));
        g.cropNorm = r;
    }

    const QJsonObject output = child(o, "output");
    p.outputSharpenAmount = numf(output, "sharpenAmount", p.outputSharpenAmount);
    p.outputSharpenRadius = numf(output, "sharpenRadius", p.outputSharpenRadius);

    *params = p;
    return true;
}

bool write(const QString& rawPath, const EditParams& p, QString* error) {
    auto fail = [&](const QString& m) { if (error) *error = m; return false; };
    QJsonObject o = toJson(p);
    o.insert(QLatin1String("source"), QFileInfo(rawPath).fileName());
    QSaveFile f(pathFor(rawPath));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return fail(f.errorString());
    f.write(QJsonDocument(o).toJson(QJsonDocument::Indented));
    if (!f.commit()) return fail(f.errorString());
    return true;
}

ReadResult read(const QString& rawPath, EditParams* params, QString* error) {
    QFile f(pathFor(rawPath));
    if (!f.exists()) return ReadResult::None;
    if (!f.open(QIODevice::ReadOnly)) { if (error) *error = f.errorString(); return ReadResult::Invalid; }
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &pe);
    if (doc.isNull() || !doc.isObject()) { if (error) *error = doc.isNull() ? pe.errorString() : QStringLiteral("not a JSON object"); return ReadResult::Invalid; }
    return fromJson(doc.object(), params, error) ? ReadResult::Loaded : ReadResult::Invalid;
}

bool remove(const QString& rawPath, QString* error) {
    QFile f(pathFor(rawPath));
    if (!f.exists()) return true;
    if (f.remove()) return true;
    if (error) *error = f.errorString();
    return false;
}

}  // namespace re::sidecar
