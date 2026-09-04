#version 430
in vec2 vUv;
out vec4 fragColor;
layout(binding = 0) uniform sampler2D uTex;
uniform vec3 uBg;
void main() {
    vec4 t = texture(uTex, vUv);
    fragColor = vec4(mix(uBg, t.rgb, t.a), 1.0);
}
