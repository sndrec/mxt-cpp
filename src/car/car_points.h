#pragma once

#include "core/sim_math.h"
#include <cstdint>

class PhysicsCarSuspensionPoint {
public:
        SimVec3 origin_point;
        SimVec3 offset;
        SimVec3 pos_old;
        SimVec3 pos;
        SimVec3 up_vector;
        SimVec3 up_vector_2;
        SimVec3 target_dir;
        float target_length = 0.0f;
        float max_length = 0.0f;
        float spring_strength = 0.0f;
        float force_at_point = 0.0f;
        float force = 0.0f;
        float rest_length = 0.0f;
        SimVec3 force_spatial;
        float force_spatial_len = 0.0f;
        uint32_t state = 0;
};

class PhysicsCarCollisionPoint {
public:
        SimVec3 origin_point;
        SimVec3 offset;
        SimVec3 pos_a;
        SimVec3 pos_b;
        SimVec3 collision;
};
