#pragma once

#include "godot_cpp/classes/engine.hpp"

class PhysicsCarSuspensionPoint {
public:
	godot::Vector3 origin_point;
	godot::Vector3 target_dir;
	float target_length;
	float max_length;
	float spring_strength;
	float force_at_point;
};

class PhysicsCarCollisionPoint {
public:
	godot::Vector3 origin_point;
};