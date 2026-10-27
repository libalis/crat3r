#version 450

layout(location = 0) in vec4 fragColor;
layout(location = 1) in vec2 uv;

layout(location = 0) out vec4 outColor;

void main() {
    float x = (uv.x < 0.2) ? (uv.x / 0.2) : (1.0 - (uv.x - 0.2) / 0.8);
    float y = 1.0 - 2.0 * abs(uv.y - 0.5);
    outColor = x * y * fragColor;
}
