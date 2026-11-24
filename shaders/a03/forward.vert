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

layout(location = 0) out vec2 fragUv;

layout(push_constant) uniform PushConstants {
  mat4 mvp;
} pc;

void main() {
  gl_Position = pc.mvp * vec4(inPos, 1.);
  fragUv = inUv;
}
