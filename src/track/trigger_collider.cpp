#include "track/trigger_collider.h"
#include "car/physics_car.h"
#include <cmath>
#include <algorithm>
#include "mxt_core/debug.hpp"

uint8_t TriggerCollider::intersect_segment(int cp_idx, const godot::Vector3 &p0, const godot::Vector3 &p1) const
{
	bool should_continue = false;
    if (cp_idx == checkpoint_index)
        should_continue = true;

   	if (!should_continue)
   	{
   		for (int i = 0; i < current_track->checkpoints[checkpoint_index].num_neighboring_checkpoints; i++)
   		{
   			if (current_track->checkpoints[checkpoint_index].neighboring_checkpoints[i] == cp_idx)
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

Dashplate::Dashplate() { type = TRIGGER_TYPE::DASHPLATE; }
void Dashplate::start_touch(PhysicsCar* car) {}
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
}
void Mine::touch(PhysicsCar* car) {}
void Mine::end_touch(PhysicsCar* car) {}

