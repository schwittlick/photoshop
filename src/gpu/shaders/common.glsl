// Shared GLSL helpers (textually included by ShaderProgram).
float srgbOETF(float x) {
    x = clamp(x, 0.0, 1.0);
    return x <= 0.0031308 ? 12.92 * x : 1.055 * pow(x, 1.0 / 2.4) - 0.055;
}
vec3 srgbOETF3(vec3 c) { return vec3(srgbOETF(c.r), srgbOETF(c.g), srgbOETF(c.b)); }
float srgbEOTF(float e) {
    e = clamp(e, 0.0, 1.0);
    return e <= 0.04045 ? e / 12.92 : pow((e + 0.055) / 1.055, 2.4);
}
vec3 srgbEOTF3(vec3 e) { return vec3(srgbEOTF(e.r), srgbEOTF(e.g), srgbEOTF(e.b)); }
