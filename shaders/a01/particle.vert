#version 450

layout(location = 0) in vec2 inQuadPos;
layout(location = 1) in vec2 inQuadUv;
layout(location = 2) in vec3 inParticlePos;
layout(location = 3) in vec3 inParticleVel;
layout(location = 4) in float inParticleLife;

layout(location = 0) out vec4 outFragColor;
layout(location = 1) out vec2 outUv;

layout(push_constant) uniform PushConstants {
  mat4 modelview;
  mat4 projection;
} pc;

const vec2 spriteSize = vec2(0.06, 0.01);

vec4 mixColor(float life) {
  const vec4 keys[16] = vec4[](
      vec4(2.00, 1.93, 1.35, 0.00),
      vec4(2.00, 1.93, 1.35, 1.00),
      vec4(1.71, 1.62, 0.95, 1.00),
      vec4(1.41, 1.31, 0.55, 1.00),
      vec4(1.11, 1.00, 0.15, 1.00),
      vec4(1.00, 0.77, 0.00, 1.00),
      vec4(1.00, 0.58, 0.00, 1.00),
      vec4(1.00, 0.39, 0.00, 1.00),
      vec4(1.00, 0.21, 0.00, 1.00),
      vec4(1.00, 0.02, 0.00, 1.00),
      vec4(1.00, 0.00, 0.05, 1.00),
      vec4(1.00, 0.00, 0.10, 1.00),
      vec4(1.00, 0.00, 0.15, 0.79),
      vec4(1.00, 0.00, 0.21, 0.51),
      vec4(1.00, 0.00, 0.26, 0.23),
      vec4(1.00, 0.00, 0.31, 0.00)
    );

  float x = (1. - life) * 16;
  float t = fract(x);
  uint idx = uint(x);

  return (1. - t) * keys[min(idx, 15)] + t * keys[min(idx + 1, 15)];
}

void main() {
  const vec3 viewVel = mat3(pc.modelview) * normalize(inParticleVel);
  const vec3 viewVelDir = normalize(vec3(viewVel.xy, 0.));
  const vec3 viewVelTangent = cross(viewVelDir, vec3(0., 0., 1.));

  const vec2 quadPosScl = inQuadPos * spriteSize;
  const vec3 quadPos = length(viewVel.xy) * viewVelDir * quadPosScl.x + viewVelTangent * quadPosScl.y;

  vec4 viewPos = pc.modelview * vec4(inParticlePos, 1.);
  viewPos += vec4(quadPos, 0);
  gl_Position = pc.projection * viewPos;

  outFragColor = mixColor(inParticleLife);
  outUv = inQuadUv;
}
