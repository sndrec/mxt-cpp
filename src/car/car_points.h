#pragma once

#include "godot_cpp/classes/engine.hpp"
#include <cstdint>

class PhysicsCarSuspensionPoint {
public:
        godot::Vector3 origin_point;
        godot::Vector3 offset;
        godot::Vector3 pos_old;
        godot::Vector3 pos;
        godot::Vector3 up_vector;
        godot::Vector3 up_vector_2;
        godot::Vector3 target_dir;
        float target_length = 0.0f;
        float max_length = 0.0f;
        float spring_strength = 0.0f;
        float force_at_point = 0.0f;
        float force = 0.0f;
        float rest_length = 0.0f;
        godot::Vector3 force_spatial;
        float force_spatial_len = 0.0f;
        uint32_t state = 0;
};

class PhysicsCarCollisionPoint {
public:
        godot::Vector3 origin_point;
        godot::Vector3 offset;
        godot::Vector3 pos_a;
        godot::Vector3 pos_b;
        godot::Vector3 collision;
};
