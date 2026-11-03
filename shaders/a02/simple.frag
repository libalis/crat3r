#version 450

layout(location = 0) in vec2 fragUv;

layout(location = 0) out vec4 outColor;

//TODO: Assignment 2.8
// Add a binding for a two-dimensional sampler and use the UV coordinates to sample the texture.

//TODO: Assignment 2.12
// Add the binding for the material uniform buffer and, in case the material does not have a texture,
// use the uniform base color instead.

void main() {
    outColor = vec4(fragUv, 0., 1.);
}
