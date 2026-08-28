#pragma once

#include "core/sim_math.h"
#include "track/racetrack.h"

#include <godot_cpp/classes/gpu_particles3d.hpp>
#include <godot_cpp/classes/material.hpp>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/shader_material.hpp>
#include <godot_cpp/variant/color.hpp>
#include <godot_cpp/variant/packed_float32_array.hpp>
#include <godot_cpp/variant/rid.hpp>

namespace godot {

struct GameSimRenderDashplateVisual {
	Dashplate *trigger = nullptr;
	Ref<ShaderMaterial> projection_material;
	float boost_time = 0.0f;
};

struct GameSimRenderBodyMultimeshBuffers {
	PackedFloat32Array main;
	PackedFloat32Array outline;
	PackedFloat32Array outline_main;
	PackedFloat32Array shadow;
	PackedFloat32Array stamp;
	float *main_write = nullptr;
	float *outline_write = nullptr;
	float *outline_main_write = nullptr;
	float *shadow_write = nullptr;
	float *stamp_write = nullptr;
};

struct GameSimRenderVehicleVisualState {
	float startup_wobble = 0.0f;
	float turn_reaction_effect = 0.0f;
	float height_adjust_from_boost = 0.0f;
	int strafe_visual_roll = 0;
	SimQuat visual_quat;
};

struct GameSimRenderVehicleEffectRefs {
	uint32_t terrain_state_old = 0;
	uint32_t machine_state_old = 0;
	uint8_t full_effect_active = 0;
	float impact_flash = 0.0f;
	Color overlay = Color(0, 0, 0, 1);
	Color energy_overlay = Color(0, 0, 0, 1);
};

struct GameSimRenderEffectPoolSlot {
	Node *node = nullptr;
	Node3D *car_transform = nullptr;
	GPUParticles3D *recharge_particles = nullptr;
	GPUParticles3D *attack_particles = nullptr;
	GPUParticles3D *landing_particles = nullptr;
	GPUParticles3D *damage_electricity = nullptr;
	GPUParticles3D *damage_smoke = nullptr;
	Ref<Material> damage_electricity_material;
	Object *boost_electricity = nullptr;
	int car_index = -1;
	uint8_t fixed_local = 0;
};

struct GameSimRenderThrusterLightRID {
	RID light;
	RID instance;
};

} // namespace godot
