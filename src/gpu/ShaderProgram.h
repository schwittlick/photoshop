#pragma once
#include "core/Math.h"
#include <QOpenGLShaderProgram>
#include <QString>
#include <QStringList>
#include <QVector2D>
#include <QVector3D>
#include <QVector4D>
#include <memory>

namespace re {

// Thin wrapper over QOpenGLShaderProgram that resolves `#include "file"` against the
// :/shaders resource prefix and injects #defines after the #version line.
class ShaderProgram {
public:
    bool compileCompute(const QString& resource, const QStringList& defines, QString* error);
    bool compileGraphics(const QString& vertResource, const QString& fragResource, QString* error);
    bool valid() const { return prog_ && prog_->isLinked(); }
    void bind() { prog_->bind(); }
    void release() { prog_->release(); }

    void set(const char* name, int v) { prog_->setUniformValue(name, v); }
    void set(const char* name, float v) { prog_->setUniformValue(name, v); }
    void set(const char* name, float x, float y) { prog_->setUniformValue(name, QVector2D(x, y)); }
    void set(const char* name, float x, float y, float z) { prog_->setUniformValue(name, QVector3D(x, y, z)); }
    void set(const char* name, float x, float y, float z, float w) { prog_->setUniformValue(name, QVector4D(x, y, z, w)); }
    void set(const char* name, const Vec3& v) { set(name, float(v.x), float(v.y), float(v.z)); }
    void setInt2(const char* name, int x, int y);  // ivec2 (Qt's QPoint overload uploads floats)
    void set(const char* name, const Mat3& m);

    static QString loadSource(const QString& resource, const QStringList& defines, QString* error);

private:
    std::unique_ptr<QOpenGLShaderProgram> prog_;
};

}  // namespace re
