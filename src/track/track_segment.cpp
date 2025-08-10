#include <math.h>
#include "mxt_core/math_utils.h"
#include "track/track_segment.h"

void RoadShape::find_t_from_relative_pos(godot::Vector2 &out_t, const godot::Vector3& in_pos) const
{
	out_t = godot::Vector2(in_pos[0], in_pos[2]);
};

void RoadShape::get_position_at_time(godot::Vector3 &out_pos, const godot::Vector2& in_t) const
{
	RoadTransform road_root;
	owning_segment->curve_matrix->sample(road_root, in_t.y);
	const float mod_t = 0.5f * (1.0f - in_t.x);
	float vertical_offset = 0.0f;
	for(int i = 0; i < num_modulations; ++i)
	{
		const float affector = road_modulations[i].modulation_effect->sample(in_t.y);
		if(affector == 0.0f)
			continue;

		vertical_offset += road_modulations[i].modulation_height->sample(mod_t) * affector;
	}
	const godot::Vector3 local_pos(in_t.x * road_root.scale.x, vertical_offset * road_root.scale.y, 0.0f);
	out_pos = road_root.t3d.xform(local_pos);
}

//void RoadShape::get_transform_at_time(godot::Transform3D &out_transform, const godot::Vector2& in_t) const
//{
//	RoadTransform root;
//	owning_segment->curve_matrix->sample(root, in_t.y);
//	const float mod_t = 0.5f * (1.0f - in_t.x);
//
//	float y_offset = 0.0f;
//	for (int i = 0; i < num_modulations; ++i)
//	{
//		const float aff = road_modulations[i].modulation_effect->sample(in_t.y);
//		if (aff == 0.0f)
//			continue;
//
//		y_offset += road_modulations[i].modulation_height->sample(mod_t) * aff;
//	}
//	const godot::Vector3 local(in_t.x, y_offset, 0.0f);
//	out_transform.basis = root.t3d.basis;
//	out_transform.origin = root.t3d.xform(local);
//}

void RoadShapePipe::find_t_from_relative_pos(godot::Vector2 &out_t, const godot::Vector3& p) const
{
        float phi = deterministic_fp::atan2f(p.y, p.x) * ONE_DIV_BY_PI + 0.5f;
        if (phi < -1.0f)
        {
                phi += 2.0f;
        }
        if (phi > 1.0f)
        {
                phi -= 2.0f;
        }
        out_t = godot::Vector2(phi, p.z);
}

void RoadShapePipe::get_position_at_time(godot::Vector3 &out_pos, const godot::Vector2& in_t) const
{
	RoadTransform road_root_transform;
	owning_segment->curve_matrix->sample(road_root_transform, in_t[1]);

    const float mod_t = 0.5f * (1.0f - in_t[0]);

	float mod_vertical_offset = 1.0f;

	for (int i = 0; i < num_modulations; i++)
	{
		const float mod_affector = road_modulations[i].modulation_effect->sample(in_t.y);
		mod_vertical_offset += road_modulations[i].modulation_height->sample(mod_t) * mod_affector;
	}

        const float tx_angle = (in_t[0] - 0.5f) * PI;
        const godot::Vector3 pos = godot::Vector3(
                deterministic_fp::cosf(tx_angle),
                deterministic_fp::sinf(tx_angle),
                0.0f);
	const godot::Vector3 dir = pos.normalized();
	const godot::Vector3 road_point = dir * mod_vertical_offset;
	const godot::Transform3D road_shape_transform = godot::Transform3D(BASIS_IDENTITY, road_point * road_root_transform.scale);
	const godot::Transform3D final_transform = road_root_transform.t3d * road_shape_transform;

	out_pos = final_transform.origin;
};

//void RoadShapePipe::get_transform_at_time(godot::Transform3D &out_transform, const godot::Vector2& in_t) const
//{
//	godot::Transform3D road_root_transform;
//	owning_segment->curve_matrix->sample(road_root_transform, in_t[1]);
//
//	const float mod_t = (in_t[0] + 1.0f) * 0.5f;
//
//	float mod_vertical_offset = 0.0f;
//
//	for (int i = 0; i < num_modulations; i++)
//	{
//		const float mod_affector = road_modulations[i].modulation_effect->sample(in_t.y);
//		mod_vertical_offset += road_modulations[i].modulation_height->sample(mod_t) * mod_affector;
//	}
//
//	const godot::Vector3 pos = godot::Vector3(deterministic_fp::cosf((in_t[0] - 0.5f) * PI), deterministic_fp::sinf((in_t[0] - 0.5f) * PI), 0.0f);
//	const godot::Vector3 dir = pos.normalized(); // direction from center of road segment to surface point
//	const godot::Vector3 road_point = pos + dir * mod_vertical_offset;
//	const godot::Vector3 left = -godot::Vector3(dir.y, -dir.x, 0.0f); // inside of pipe, so normal should be -dir
//	const godot::Transform3D road_shape_transform = godot::Transform3D(godot::Basis(left, -dir, godot::Vector3(.0f, .0f, 1.0f)), road_point);
//	const godot::Transform3D final_transform = road_root_transform * road_shape_transform;
//
//	out_transform = final_transform;
//};

void RoadShapeCylinder::find_t_from_relative_pos(godot::Vector2 &out_t, const godot::Vector3& in_pos) const
{
        const float theta = deterministic_fp::atan2f(in_pos.x, in_pos.y);
        out_t = godot::Vector2(theta * ONE_DIV_BY_PI, in_pos.z);
};

void RoadShapeCylinder::get_position_at_time(godot::Vector3 &out_pos, const godot::Vector2& in_t) const
{
	RoadTransform road_root_transform;
	owning_segment->curve_matrix->sample(road_root_transform, in_t[1]);

        const float mod_t = 0.5f * (1.0f - in_t[0]);

	float mod_vertical_offset = 1.0f;

	for (int i = 0; i < num_modulations; i++)
	{
		const float mod_affector = road_modulations[i].modulation_effect->sample(in_t.y);
		mod_vertical_offset += road_modulations[i].modulation_height->sample(mod_t) * mod_affector;
	}

        const float theta = in_t.x * PI;
        const godot::Vector3 pos = godot::Vector3(
                deterministic_fp::sinf(theta),
                deterministic_fp::cosf(theta),
                0.0f);
	const godot::Vector3 dir = pos.normalized();
	const godot::Vector3 road_point = dir * mod_vertical_offset;
	godot::Transform3D road_shape_transform = T3D_IDENTITY;
	road_shape_transform.origin = road_point * road_root_transform.scale;
	const godot::Transform3D final_transform = road_root_transform.t3d * road_shape_transform;

	out_pos = final_transform.origin;
};

//void RoadShapeCylinder::get_transform_at_time(godot::Transform3D &out_transform, const godot::Vector2& in_t) const
//{
//	godot::Transform3D road_root_transform;
//	owning_segment->curve_matrix->sample(road_root_transform, in_t[1]);
//
//	const float mod_t = 1.0f - (in_t[0] + 1.0f) * 0.5f;
//
//	float mod_vertical_offset = 0.0f;
//
//	for (int i = 0; i < num_modulations; i++)
//	{
//		const float mod_affector = road_modulations[i].modulation_effect->sample(in_t.y);
//		mod_vertical_offset += road_modulations[i].modulation_height->sample(mod_t) * mod_affector;
//	}
//
//	const godot::Vector3 pos = godot::Vector3(deterministic_fp::sinf((in_t.x - 0.5f) * PI), deterministic_fp::cosf((in_t.x - 0.5f) * PI), 0.0f);
//	const godot::Vector3 dir = pos.normalized();
//	const godot::Vector3 road_point = pos + dir * mod_vertical_offset;
//	godot::Transform3D road_shape_transform = T3D_IDENTITY;
//	road_shape_transform.origin = road_point;
//	const godot::Vector3 left = godot::Vector3(dir.x, -dir.y, .0f);
//	road_shape_transform.basis = godot::Basis(left, dir, godot::Vector3(.0f, .0f, 1.0f));
//	const godot::Transform3D final_transform = road_root_transform * road_shape_transform;
//
//	out_transform = final_transform;
//};

void RoadShapePipeOpen::find_t_from_relative_pos(godot::Vector2 &out_t, const godot::Vector3& in_pos) const
{
        float phi = deterministic_fp::atan2f(in_pos.y, in_pos.x) * ONE_DIV_BY_PI + 0.5f;
        if (phi < -1.0f)
        {
                phi += 2.0f;
        }
        if (phi > 1.0f)
        {
                phi -= 2.0f;
        }
        phi /= fmaxf(0.001f, openness->sample(in_pos[2]));
        out_t = godot::Vector2(phi, in_pos[2]);
};

void RoadShapePipeOpen::get_position_at_time(godot::Vector3 &out_pos, const godot::Vector2& in_t) const
{
	RoadTransform road_root_transform;
	owning_segment->curve_matrix->sample(road_root_transform, in_t[1]);

        float mod_tx = in_t[0] * openness->sample(in_t[1]);
        const float mod_t = 0.5f * (1.0f - in_t[0]);

	float mod_vertical_offset = 1.0f;

	for (int i = 0; i < num_modulations; i++)
	{
		const float mod_affector = road_modulations[i].modulation_effect->sample(in_t.y);
		mod_vertical_offset += road_modulations[i].modulation_height->sample(mod_t) * mod_affector;
	}

        const float tx_angle = (mod_tx - 0.5f) * PI;
        const godot::Vector3 pos = godot::Vector3(
                deterministic_fp::cosf(tx_angle),
                deterministic_fp::sinf(tx_angle),
                0.0f);
	const godot::Vector3 dir = pos.normalized();
	const godot::Vector3 road_point = dir * mod_vertical_offset;
	godot::Transform3D road_shape_transform = T3D_IDENTITY;
	road_shape_transform.origin = road_point * road_root_transform.scale;
	const godot::Transform3D final_transform = road_root_transform.t3d * road_shape_transform;

	out_pos = final_transform.origin;
};

//void RoadShapePipeOpen::get_transform_at_time(godot::Transform3D &out_transform, const godot::Vector2& in_t) const
//{
//	godot::Transform3D road_root_transform;
//	owning_segment->curve_matrix->sample(road_root_transform, in_t[1]);
//
//	const float mod_t = 1.0f - (in_t[0] + 1.0f) * 0.5f;
//
//	float mod_vertical_offset = 0.0f;
//
//	for (int i = 0; i < num_modulations; i++)
//	{
//		const float mod_affector = road_modulations[i].modulation_effect->sample(in_t.y);
//		mod_vertical_offset += road_modulations[i].modulation_height->sample(mod_t) * mod_affector;
//	}
//
//	const float mod_tx = in_t[0] * openness->sample(in_t[1]);
//
//	const godot::Vector3 pos = godot::Vector3(deterministic_fp::cosf((mod_tx - 0.5f) * PI), deterministic_fp::sinf((mod_tx - 0.5f) * PI), 0.0f);
//	const godot::Vector3 dir = pos.normalized();
//	const godot::Vector3 road_point = pos + dir * mod_vertical_offset;
//	godot::Transform3D road_shape_transform = T3D_IDENTITY;
//	road_shape_transform.origin = road_point;
//	const godot::Vector3 left = godot::Vector3(-dir.x, dir.y, .0f);
//	road_shape_transform.basis = godot::Basis(left, -dir, godot::Vector3(.0f, .0f, 1.0f));
//	const godot::Transform3D final_transform = road_root_transform * road_shape_transform;
//
//	out_transform = final_transform;
//};


void RoadShapeCylinderOpen::find_t_from_relative_pos(godot::Vector2 &out_t, const godot::Vector3& in_pos) const
{
        float tx = deterministic_fp::atan2f(in_pos.x, in_pos.y) * ONE_DIV_BY_PI;
        tx /= fmaxf(0.001f, openness->sample(in_pos[2]));
        out_t = godot::Vector2(tx, in_pos[2]);
}

void RoadShapeCylinderOpen::get_position_at_time(godot::Vector3 &out_pos, const godot::Vector2& in_t) const
{
	RoadTransform road_root_transform;
	owning_segment->curve_matrix->sample(road_root_transform, in_t[1]);

        float mod_tx = in_t[0] * openness->sample(in_t[1]);
        const float mod_t = 0.5f * (1.0f - in_t[0]);

	float mod_vertical_offset = 1.0f;

	for (int i = 0; i < num_modulations; i++)
	{
		const float mod_affector = road_modulations[i].modulation_effect->sample(in_t.y);
		mod_vertical_offset += road_modulations[i].modulation_height->sample(mod_t) * mod_affector;
	}

        const float theta = mod_tx * PI;
        const godot::Vector3 pos = godot::Vector3(
                deterministic_fp::sinf(theta),
                deterministic_fp::cosf(theta),
                0.0f);
	const godot::Vector3 dir = pos.normalized();
	const godot::Vector3 road_point = dir * mod_vertical_offset;
	godot::Transform3D road_shape_transform = T3D_IDENTITY;
	road_shape_transform.origin = road_point * road_root_transform.scale;
	const godot::Transform3D final_transform = road_root_transform.t3d * road_shape_transform;

        out_pos = final_transform.origin;
};

static inline godot::Vector2 _closest_point_on_rounded_rect(const godot::Vector2 &p, float w, float h, float r)
{
        const float w2 = 0.5f * w;
        const float h2 = 0.5f * h;
        const float radius = fminf(fmaxf(r, 0.0f), fminf(w2, h2));
        const float inner_x = w2 - radius;
        const float inner_y = h2 - radius;

        godot::Vector2 pp(fabsf(p.x), fabsf(p.y));
        godot::Vector2 result;

        if (pp.x > inner_x && pp.y > inner_y)
        {
                godot::Vector2 corner_center(inner_x, inner_y);
                godot::Vector2 off = pp - corner_center;
                const float len = off.length();
                if (len > 1.0e-6f)
                        off *= radius / len;
                else
                        off = godot::Vector2(radius, 0.0f);
                result = corner_center + off;
        }
        else if (pp.x > inner_x)
        {
                result = godot::Vector2(w2, pp.y);
        }
        else if (pp.y > inner_y)
        {
                result = godot::Vector2(pp.x, h2);
        }
        else
        {
                const float dx = w2 - pp.x;
                const float dy = h2 - pp.y;
                if (dx < dy)
                        result = godot::Vector2(w2, pp.y);
                else
                        result = godot::Vector2(pp.x, h2);
        }

        result.x = copysignf(result.x, p.x);
        result.y = copysignf(result.y, p.y);
        return result;
}

void RoadShapeRoundedRect::find_t_from_relative_pos(godot::Vector2 &out_t, const godot::Vector3& p) const
{
        const float w = width->sample(p.z);
        const float h = height->sample(p.z);
        const float r = radius->sample(p.z);

        const godot::Vector2 closest = _closest_point_on_rounded_rect(godot::Vector2(p.x, p.y), w, h, r);

        float theta = deterministic_fp::atan2f(closest.x, closest.y);
        if (theta < 0.0f)
        {
                theta += PI * 2.0f;
        }
        float tx = 1.0f - theta * ONE_DIV_BY_PI;
        if (tx > 1.0f)
        {
                tx -= 2.0f;
        }
        if (tx < -1.0f)
        {
                tx += 2.0f;
        }
        out_t = godot::Vector2(tx, p.z);
};

static inline float _rounded_rect_length(const godot::Vector2 &dir, float w, float h, float r)
{
	const float w2 = 0.5f * w;
	const float h2 = 0.5f * h;
	const float radius = fminf(fmaxf(r, 0.0f), fminf(w2, h2));

	const float abs_dx = fabsf(dir.x);
	const float abs_dy = fabsf(dir.y);

	float min_t = FLT_MAX;

	// try vertical side (x = ±w2)
	if (abs_dx > 1.0e-6f) {
		const float t_v = w2 / abs_dx;
		if (t_v * abs_dy <= h2 - radius + 1.0e-6f)
			min_t = t_v;
	}

	// try horizontal side (y = ±h2)
	if (abs_dy > 1.0e-6f) {
		const float t_h = h2 / abs_dy;
		if (t_h * abs_dx <= w2 - radius + 1.0e-6f)
			min_t = fminf(min_t, t_h);
	}

	// try corner radius
	if (radius > 1.0e-6f) {
		const float w_in = fmaxf(w2 - radius, 0.0f);
		const float h_in = fmaxf(h2 - radius, 0.0f);

		const float b = -2.0f * (abs_dx * w_in + abs_dy * h_in);
		const float c = w_in * w_in + h_in * h_in - radius * radius;
		const float disc = b * b - 4.0f * c;

		if (disc >= 0.0f) {
			const float sqrt_d = sqrtf(disc);
			const float root1 = 0.5f * (-b - sqrt_d);
			const float root2 = 0.5f * (-b + sqrt_d);

			if (root1 > 0.0f &&
				root1 * abs_dx >= w_in - 1.0e-6f &&
				root1 * abs_dy >= h_in - 1.0e-6f)
				min_t = fminf(min_t, root1);

			if (root2 > 0.0f &&
				root2 * abs_dx >= w_in - 1.0e-6f &&
				root2 * abs_dy >= h_in - 1.0e-6f)
				min_t = fminf(min_t, root2);
		}
	}

	// fallback: degenerate case (e.g., extreme aspect ratio)
	if (min_t == FLT_MAX)
		min_t = fminf(w2, h2);

	godot::Vector2 p = dir * min_t;
	return p.length();
}

void RoadShapeRoundedRect::get_position_at_time(godot::Vector3 &out_pos, const godot::Vector2& in_t) const
{
        RoadTransform road_root_transform;
        owning_segment->curve_matrix->sample(road_root_transform, in_t[1]);

        const float mod_t = 0.5f * (1.0f - in_t[0]);

        float mod_vertical_offset = 1.0f;
        for (int i = 0; i < num_modulations; i++)
        {
                const float mod_affector = road_modulations[i].modulation_effect->sample(in_t.y);
                mod_vertical_offset += road_modulations[i].modulation_height->sample(mod_t) * mod_affector;
        }

        const float w = width->sample(in_t[1]);
        const float h = height->sample(in_t[1]);
        const float r = radius->sample(in_t[1]);

        const float theta = (1.0f - in_t[0]) * PI;
        const godot::Vector2 dir(deterministic_fp::sinf(theta), deterministic_fp::cosf(theta));

        float length = _rounded_rect_length(dir, w, h, r);
        length *= mod_vertical_offset;
        const godot::Vector2 final = dir * length;

        godot::Transform3D road_shape_transform = T3D_IDENTITY;
        road_shape_transform.origin = godot::Vector3(final.x, final.y, 0.0f) * road_root_transform.scale;
        const godot::Transform3D final_transform = road_root_transform.t3d * road_shape_transform;

        out_pos = final_transform.origin;
};

void RoadShapeRoundedRectOpen::find_t_from_relative_pos(godot::Vector2 &out_t, const godot::Vector3& p) const
{
        const float w = width->sample(p.z);
        const float h = height->sample(p.z);
        const float r = radius->sample(p.z);

        const godot::Vector2 closest = _closest_point_on_rounded_rect(godot::Vector2(p.x, p.y), w, h, r);

        float theta = deterministic_fp::atan2f(closest.x, closest.y);
        if (theta < 0.0f)
        {
                theta += PI * 2.0f;
        }
        float mod_tx = 1.0f - theta * ONE_DIV_BY_PI;
        float openness_v = fmaxf(0.001f, openness->sample(p.z));
        float tx = mod_tx / openness_v;
        out_t = godot::Vector2(tx, p.z);
};

void RoadShapeRoundedRectOpen::get_position_at_time(godot::Vector3 &out_pos, const godot::Vector2& in_t) const
{
        RoadTransform road_root_transform;
        owning_segment->curve_matrix->sample(road_root_transform, in_t[1]);

        float mod_tx = in_t[0] * openness->sample(in_t[1]);
        const float mod_t = 0.5f * (1.0f - in_t[0]);

        float mod_vertical_offset = 1.0f;
        for (int i = 0; i < num_modulations; i++)
        {
                const float mod_affector = road_modulations[i].modulation_effect->sample(in_t.y);
                mod_vertical_offset += road_modulations[i].modulation_height->sample(mod_t) * mod_affector;
        }

        const float w = width->sample(in_t[1]);
        const float h = height->sample(in_t[1]);
        const float r = radius->sample(in_t[1]);

        const float theta = (1.0f - mod_tx) * PI;
        const godot::Vector2 dir(deterministic_fp::sinf(theta), deterministic_fp::cosf(theta));

        float length = _rounded_rect_length(dir, w, h, r);
        length *= mod_vertical_offset;
        const godot::Vector2 final = dir * length;

        godot::Transform3D road_shape_transform = T3D_IDENTITY;
        road_shape_transform.origin = godot::Vector3(final.x, final.y, 0.0f) * road_root_transform.scale;
        const godot::Transform3D final_transform = road_root_transform.t3d * road_shape_transform;

        out_pos = final_transform.origin;
};

//void RoadShapeCylinderOpen::get_transform_at_time(godot::Transform3D &out_transform, const godot::Vector2& in_t) const
//{
//	godot::Transform3D road_root_transform;
//	owning_segment->curve_matrix->sample(road_root_transform, in_t[1]);
//
//	const float mod_t = 1.0f - (in_t[0] + 1.0f) * 0.5f;
//
//	float mod_vertical_offset = 0.0f;
//
//	for (int i = 0; i < num_modulations; i++)
//	{
//		const float mod_affector = road_modulations[i].modulation_effect->sample(in_t.y);
//		mod_vertical_offset += road_modulations[i].modulation_height->sample(mod_t) * mod_affector;
//	}
//
//	const godot::Vector3 pos = godot::Vector3(deterministic_fp::sinf((in_t[0] - 0.5f) * PI), deterministic_fp::cosf((in_t[0] - 0.5f) * PI), 0.0f);
//	const godot::Vector3 dir = pos.normalized();
//	const godot::Vector3 road_point = pos + dir * mod_vertical_offset;
//	godot::Transform3D road_shape_transform = T3D_IDENTITY;
//	road_shape_transform.origin = road_point;
//	const godot::Vector3 left = godot::Vector3(dir.x, -dir.y, .0f);
//	road_shape_transform.basis = godot::Basis(left, dir, godot::Vector3(.0f, .0f, 1.0f));
//	const godot::Transform3D final_transform = road_root_transform * road_shape_transform;
//
//	out_transform = final_transform;
//};

const float transform_epsilon = 0.002f;

void RoadShape::get_oriented_transform_at_time(godot::Transform3D &out_transform, const godot::Vector2& in_t) const
{
	godot::Vector3 base_pos;
	get_position_at_time(base_pos, in_t);

	const float sign_x = (in_t.x > 0.0f) ? -1.0f : 1.0f;	// which side of the centre‑line
	const float sign_y = (in_t.y < 0.5f) ? 1.0f : -1.0f;	// front or back half
	const float right_off = sign_x * transform_epsilon;
	const float fwd_off = sign_y * (transform_epsilon * 100.0f / owning_segment->segment_length);

	godot::Vector3 pos_right;
	get_position_at_time(pos_right, in_t + godot::Vector2(right_off, 0.0f));
	pos_right -= base_pos;
	godot::Vector3 pos_forward;
	get_position_at_time(pos_forward, in_t + godot::Vector2(0.0f, fwd_off));
	pos_forward -= base_pos;

    float normal_sign = -(sign_x * sign_y);
    godot::Vector3 normal = normal_sign * pos_right.cross(pos_forward);

	pos_right.normalize();
	normal.normalize();
	pos_forward.normalize();

	out_transform.basis[0] = sign_x * pos_right;
	out_transform.basis[1] = normal;
	out_transform.basis[2] = sign_y * pos_forward;
	out_transform.origin = base_pos;
}
