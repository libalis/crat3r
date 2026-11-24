#version 450

// TODO: Assignment 3.1
// Add inputs for the fragment normal and position.
// You can write these to the output color for debugging.
// Remember to normalize the normals!

// TODO: Assignment 3.2
// First, add the frame uniform buffer at binding 1 of set 1. Then, perform simple Blinn-Phong lighting using
// the new parameters. Calculate the ambient, diffuse, and specular terms and apply them to the output color.
// As we are not performing physically accurate shading yet, you may ignore the sun's intensity for now.
// Use some large value (e.g. 100) for the specular exponent.

// TODO: Assignment 3.3
// Add the metallic and roughness material parameters and implement the Cook-Torrance BRDF. Remember that we
// only have a single infinetely far away directional light source with a constant intensity and color for now.

// TODO: Assignment 3.4
// First, add the shadow sampler. You can display the contents of the shadow map with this very dirty trick:
// outColor = vec4(vec3(texture(shadowSampler, gl_FragCoord.xy / vec2(1920, 1080)).r), 1.0);
//
// Next, add an input for the light-space fragment position. Then, to determine whether the fragment is in shadow,
// first, perform the perspective divide on the light-space fragment position. The resulting xy-position is the
// shadow map coordinate in the range [-1,1]. The z-coordinate is the distance from the fragment to the light.
// Look up the depth in the shadow map and compare the depth values to determine if the fragment is in shadow.
// If so, set the diffuse and specular terms to zero. You may now notice severe shadow acne. Add a small depth
// bias to counteract this phenomenon.

// TODO: Assignment 3.5
// Now, implement percentage-closer filtering for the shadow evaluation. Instead of sampling the shadow map at
// a single location, which creates hard shadows, sample it at multiple locations. Iterate through a small
// kernel (e.g. 3x3) and apply a small offset before sampling the shadow map. Hint: you may use 'textureSize'
// to obtain the size of a texture.

// TODO: Assignment 3.6
// For point lights, first declare a light struct and add the array of point lights to the uniform buffer.

layout(location = 0) in vec2 fragUv;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec3 worldPosition;

layout(location = 0) out vec4 outColor;

layout(binding = 0) uniform sampler2D texSampler;

layout(binding = 1) uniform UniformBuffer {
  vec4 baseColor;
  uint hasTexture;
} ubo;

const float PI = 3.14159265358979323846264338327950288;
const float INV_PI = 0.31831015504887652430775499030746;

void main() {
  outColor = (ubo.hasTexture != 0) ? texture(texSampler, fragUv) : ubo.baseColor;
  outColor = vec4(normalize(inNormal), 1.);
  outColor = vec4(worldPosition, 1.);
}
