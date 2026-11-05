#version 450


//TODO: Assignment 2.2
// Remove the hard-coded vertex data and replace it with two inputs for the position and UV coordiate.
// Then update the main function to use these vertex inputs.

layout (location = 0) in vec3 inPosition;
layout (location = 1) in vec2 inUV;

//TODO: Assignment 2.4
// Add a push constant input containing a single mat4 for the model view projection matrix.
// Then, apply it to the vertex position before outputting it to the rasterizer.

layout(location = 0) out vec2 fragUv;

void main() {
    gl_Position = vec4(inPosition, 1.);
    fragUv = inUV;
}
