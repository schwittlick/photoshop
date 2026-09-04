#include "gpu/TexturePool.h"

namespace re {

bool TexturePool::ensure(Texture2D& t, int w, int h, GLenum fmt) {
    if (t.matches(w, h, fmt)) return false;
    destroy(t);
    gl_->glGenTextures(1, &t.id);
    gl_->glBindTexture(GL_TEXTURE_2D, t.id);
    gl_->glTexStorage2D(GL_TEXTURE_2D, 1, fmt, w, h);
    gl_->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    gl_->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    gl_->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl_->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    gl_->glBindTexture(GL_TEXTURE_2D, 0);
    t.w = w; t.h = h; t.fmt = fmt;
    return true;
}

void TexturePool::destroy(Texture2D& t) {
    if (t.id) gl_->glDeleteTextures(1, &t.id);
    t = Texture2D{};
}

}  // namespace re
