#include "gpu/ShaderProgram.h"
#include <QFile>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QMatrix3x3>
#include <QRegularExpression>

// The shader resources live in a static library, so nothing in the final link
// references the rcc-generated object and the linker drops it. Touching the
// initialiser keeps it. Q_INIT_RESOURCE must be called from the global namespace.
static void initShaderResources() {
    Q_INIT_RESOURCE(shaders);
}

namespace re {

QString ShaderProgram::loadSource(const QString& resource, const QStringList& defines, QString* error) {
    [[maybe_unused]] static const bool resourcesReady = (initShaderResources(), true);
    QFile f(resource);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error) *error = QStringLiteral("cannot open shader %1").arg(resource);
        return {};
    }
    QString src = QString::fromUtf8(f.readAll());
    static const QRegularExpression inc(R"rx(^\s*#include\s+"([^"]+)"\s*$)rx", QRegularExpression::MultilineOption);
    QRegularExpressionMatch m;
    int guard = 0;
    while ((m = inc.match(src)).hasMatch() && guard++ < 32) {
        QFile g(QStringLiteral(":/shaders/") + m.captured(1));
        if (!g.open(QIODevice::ReadOnly)) {
            if (error) *error = QStringLiteral("cannot open include %1").arg(m.captured(1));
            return {};
        }
        src.replace(m.capturedStart(0), m.capturedLength(0), QString::fromUtf8(g.readAll()));
    }
    if (!defines.isEmpty()) {
        int nl = src.indexOf('\n');
        QString defs;
        for (const QString& d : defines) defs += QStringLiteral("#define %1\n").arg(d);
        src.insert(nl + 1, defs);
    }
    return src;
}

bool ShaderProgram::compileCompute(const QString& resource, const QStringList& defines, QString* error) {
    QString src = loadSource(resource, defines, error);
    if (src.isEmpty()) return false;
    prog_ = std::make_unique<QOpenGLShaderProgram>();
    if (!prog_->addShaderFromSourceCode(QOpenGLShader::Compute, src) || !prog_->link()) {
        if (error) *error = QStringLiteral("%1: %2").arg(resource, prog_->log());
        prog_.reset();
        return false;
    }
    return true;
}

bool ShaderProgram::compileGraphics(const QString& vertResource, const QString& fragResource, QString* error) {
    QString vs = loadSource(vertResource, {}, error), fs = loadSource(fragResource, {}, error);
    if (vs.isEmpty() || fs.isEmpty()) return false;
    prog_ = std::make_unique<QOpenGLShaderProgram>();
    if (!prog_->addShaderFromSourceCode(QOpenGLShader::Vertex, vs) || !prog_->addShaderFromSourceCode(QOpenGLShader::Fragment, fs)
        || !prog_->link()) {
        if (error) *error = QStringLiteral("%1/%2: %3").arg(vertResource, fragResource, prog_->log());
        prog_.reset();
        return false;
    }
    return true;
}

void ShaderProgram::setInt2(const char* name, int x, int y) {
    int loc = prog_->uniformLocation(name);
    if (loc >= 0) QOpenGLContext::currentContext()->functions()->glUniform2i(loc, x, y);
}

void ShaderProgram::set(const char* name, const Mat3& m) {
    float v[9];
    for (int r = 0; r < 3; ++r) for (int c = 0; c < 3; ++c) v[r * 3 + c] = float(m.m[r][c]);
    prog_->setUniformValue(name, QMatrix3x3(v));  // QGenericMatrix takes row-major input
}

}  // namespace re
