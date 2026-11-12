#version 450

layout(location = 0) in vec2 fragUv;

layout(location = 0) out vec4 outColor;

//TODO: Assignment 2.8
// Add a binding for a two-dimensional sampler and use the UV coordinates to sample the texture.

layout(set = 0, binding = 0) uniform sampler2D textureSampler;

//TODO: Assignment 2.12
// Add the binding for the material uniform buffer and, in case the material does not have a texture,
// use the uniform base color instead.

layout(set = 0, binding = 1) uniform MaterialUniformBuffer {
    vec4 baseColor;
    int hasTexture;
} materialUniformBuffer;

void main() {
    if(materialUniformBuffer.hasTexture != 0 && texture(textureSampler, fragUv).a == 0.f ||
       materialUniformBuffer.hasTexture == 0 && materialUniformBuffer.baseColor.a == 0.f)
        discard;
    outColor = materialUniformBuffer.hasTexture != 0 ? texture(textureSampler, fragUv) : materialUniformBuffer.baseColor;
}
