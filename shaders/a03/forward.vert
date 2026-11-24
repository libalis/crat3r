#version 450

// TODO: Assignment 3.1
// First, add an input for the vertex normals and pass them to the fragment shader.
//
// Later on, add a uniform buffer at binding zero of set one that contains the view-projection matrix.
// Update the push constants, which now hold the model and normal matrices. Then, update the position calculations
// and apply the normal matrix. Additionally, pass the world-space position to the fragment shader.

// TODO: Assignment 3.4
// Add the sun's view projection matrix to the uniform buffer. Then transform the vertex position to light-space
// and pass it to the fragment shader. Remember that the perspective divide has to be done in the fragment shader!

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec2 inUv;
layout(location = 2) in vec3 inNormal;

layout(location = 0) out vec2 fragUv;
layout(location = 1) out vec3 outNormal;
layout(location = 2) out vec3 worldPosition;

layout(push_constant) uniform PushConstants {
  mat4 model;
  mat4 normal;
} pc;

layout(set = 1, binding = 0) uniform UniformBuffer {
  mat4 VP;
} ubo;

void main() {
  gl_Position = ubo.VP * pc.model * vec4(inPos, 1.);
  fragUv = inUv;
  outNormal = (pc.normal * vec4(inNormal, 0.)).xyz;
  worldPosition = (pc.model * vec4(inPos, 1.)).xyz;
}
