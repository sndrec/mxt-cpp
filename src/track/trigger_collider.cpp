#include "track/trigger_collider.h"
#include "car/physics_car.h"
#include <cmath>
#include <algorithm>
#include "mxt_core/debug.hpp"
#include "godot_cpp/variant/utility_functions.hpp"

namespace {
struct DashplateHeatPoint {
	float time;
	float delta;
};

constexpr DashplateHeatPoint kDashplateHeatPoints[] = {
	{0.0f, 0.0f},
	{1.0f, 1.0f},
	{2.0f, 1.0f},
	{42.0f, -20.0f}
};

constexpr int kDashplateHeatPointCount = static_cast<int>(sizeof(kDashplateHeatPoints) / sizeof(kDashplateHeatPoints[0]));
constexpr float kDashplateSecondsPerTick = 1.0f / 60.0f;

static float sample_dashplate_heat_delta(float elapsed_seconds)
{
	if (elapsed_seconds <= kDashplateHeatPoints[0].time)
		return kDashplateHeatPoints[0].delta;

	for (int i = 1; i < kDashplateHeatPointCount; ++i) {
		if (elapsed_seconds <= kDashplateHeatPoints[i].time) {
			float t0 = kDashplateHeatPoints[i - 1].time;
			float t1 = kDashplateHeatPoints[i].time;
			float v0 = kDashplateHeatPoints[i - 1].delta;
			float v1 = kDashplateHeatPoints[i].delta;
			float denom = t1 - t0;
			if (fabsf(denom) < 1e-6f)
				return v1;
			float alpha = (elapsed_seconds - t0) / denom;
			return v0 + alpha * (v1 - v0);
		}
	}

	return kDashplateHeatPoints[kDashplateHeatPointCount - 1].delta;
}
}

uint8_t TriggerCollider::intersect_segment(int cp_idx, RaceTrack *in_racetrack, const godot::Vector3 &p0, const godot::Vector3 &p1) const
{
	bool should_continue = false;
    if (cp_idx == checkpoint_index)
        should_continue = true;

   	if (!should_continue)
   	{
   		for (int i = 0; i < in_racetrack->checkpoints[checkpoint_index].num_neighboring_checkpoints; i++)
   		{
   			if (in_racetrack->checkpoints[checkpoint_index].neighboring_checkpoints[i] == cp_idx)
   			{
   				should_continue = true;
   				break;
   			}
   		}
   	}
   	if (!should_continue)
   	{
   		return 0;
   	}
   	//DEBUG::disp_text("real test", cp_idx);
	//godot::Object* dd3d = godot::Engine::get_singleton()->get_singleton("DebugDraw3D");
	//godot::Transform3D use_transform;
	//use_transform = transform.scaled_local(half_extents * 2.0);
	//dd3d->call("draw_box_xf", use_transform, godot::Color(1.0f, 1.0f, 1.0f, 1.0f), true, 0.1f);
   	//DEBUG::disp_text("trigger origin", use_transform.origin);
   	//DEBUG::disp_text("trigger basis", use_transform.basis);

    const godot::Vector3 p0_local = inv_transform.xform(p0);
    const godot::Vector3 p1_local = inv_transform.xform(p1);

    const godot::Vector3 min = -half_extents;
    const godot::Vector3 max = half_extents;

    auto point_inside = [&](const godot::Vector3 &p) -> bool {
        return (p.x >= min.x && p.x <= max.x &&
                p.y >= min.y && p.y <= max.y &&
                p.z >= min.z && p.z <= max.z);
    };

    if (p0_local.is_equal_approx(p1_local)) {
		uint8_t result = 0;
		if (point_inside(p0_local))
			result |= 0x1;
		return result;
	}

    const bool p0_in = point_inside(p0_local);
    const bool p1_in = point_inside(p1_local);

    float tmin = 0.0f;
    float tmax = 1.0f;
    const godot::Vector3 d = p1_local - p0_local;

    for (int axis = 0; axis < 3; ++axis) {
        const float start = p0_local[axis];
        const float dir   = d[axis];

        if (fabsf(dir) < 1e-8f) {
            if (start < min[axis] || start > max[axis]) {
                uint8_t result = 0;
                if (p0_in || p1_in)
                    result |= 0x1;
                if (!p0_in)
                    result |= 0x2;
                if (!p1_in)
                    result |= 0x4;
                return result;
            }
            continue;
        }

        const float inv_d = 1.0f / dir;
        float t1 = (min[axis] - start) * inv_d;
        float t2 = (max[axis] - start) * inv_d;
        if (t1 > t2)
            std::swap(t1, t2);
        if (t1 > tmin)
            tmin = t1;
        if (t2 < tmax)
            tmax = t2;
        if (tmin > tmax)
            break;
    }

    const bool hit = (tmin <= tmax) && tmax >= 0.0f && tmin <= 1.0f;

    uint8_t result = 0;
    if (p0_in || p1_in || hit)
        result |= 0x1;
    if (!p0_in)
        result |= 0x2;
    if (!p1_in)
        result |= 0x4;

    return result;
}

void TriggerCollider::start_touch(PhysicsCar* car) {}
void TriggerCollider::touch(PhysicsCar* car) {}
void TriggerCollider::end_touch(PhysicsCar* car) {}

Dashplate::Dashplate()
{
	type = TRIGGER_TYPE::DASHPLATE;
	heat = 0.0f;
	last_activation_tick = 0;
	has_last_activation = false;
}

void Dashplate::start_touch(PhysicsCar* car)
{
	if (!car)
		return;

	uint32_t current_tick = car->simulation_tick;
	float elapsed_seconds = 0.0f;
	if (has_last_activation) {
		if (current_tick >= last_activation_tick) {
			uint32_t tick_delta = current_tick - last_activation_tick;
			elapsed_seconds = static_cast<float>(tick_delta) * kDashplateSecondsPerTick;
		}
	} else {
		has_last_activation = true;
	}

	float delta_heat = has_last_activation ? sample_dashplate_heat_delta(elapsed_seconds) : 0.0f;
	heat += delta_heat;
	if (heat < kHeatMin)
		heat = kHeatMin;
	if (heat > kHeatMax)
		heat = kHeatMax;

	last_activation_tick = current_tick;

	float turbo_multiplier = 1.0f + heat * kHeatTurboMultiplier;
	car->dashplate_heat_multiplier = turbo_multiplier;

	godot::UtilityFunctions::print("Dashplate heat:", heat);
	int spark_count = static_cast<int>(std::round(heat));
	spark_count = std::clamp(spark_count, 0, 10);
	if (spark_count > 0) {
		car->queue_super_sparks(spark_count);
	}
}

void Dashplate::touch(PhysicsCar* car) {}
void Dashplate::end_touch(PhysicsCar* car) {}

Jumpplate::Jumpplate() { type = TRIGGER_TYPE::JUMPPLATE; }
void Jumpplate::start_touch(PhysicsCar* car) {}
void Jumpplate::touch(PhysicsCar* car) {}
void Jumpplate::end_touch(PhysicsCar* car) {}

Mine::Mine()
{
	type = TRIGGER_TYPE::MINE;
	exploded = false;
}
void Mine::start_touch(PhysicsCar* car)
{
	exploded = true;
	if (car) {
		car->queue_super_sparks(12);
	}
}
void Mine::touch(PhysicsCar* car) {}
void Mine::end_touch(PhysicsCar* car) {}
