#version 430
out vec2 vUv;
void main() {
    vec2 pos = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    gl_Position = vec4(pos * 2.0 - 1.0, 0.0, 1.0);
    vUv = vec2(pos.x, 1.0 - pos.y);  // texel row 0 is the top of the image
}
