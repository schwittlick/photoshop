#pragma once
#include <QOpenGLFunctions_4_3_Core>

namespace re {

struct Texture2D {
    GLuint id = 0;
    int w = 0, h = 0;
    GLenum fmt = 0;
    bool matches(int w_, int h_, GLenum fmt_) const { return id && w == w_ && h == h_ && fmt == fmt_; }
};

// Keeps stage textures alive between frames and reallocates only when a size or
// format changes. Filtering is linear + clamp-to-edge, which is what the warp needs.
class TexturePool {
public:
    explicit TexturePool(QOpenGLFunctions_4_3_Core* gl) : gl_(gl) {}
    // Returns true if the texture was (re)allocated.
    bool ensure(Texture2D& t, int w, int h, GLenum internalFormat);
    void destroy(Texture2D& t);

private:
    QOpenGLFunctions_4_3_Core* gl_;
};

}  // namespace re
