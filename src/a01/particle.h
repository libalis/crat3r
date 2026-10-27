#pragma once

#include <volk.h>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>

struct Particle {
    glm::vec4 pos; // xyz = position, w = total lifetime
    glm::vec4 vel; // xyz = velocity, w = current life
};
