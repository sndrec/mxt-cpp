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
	const float angle = deterministic_fp::atan2f(p.x, -p.y) * ONE_DIV_BY_PI;
	out_t = godot::Vector2(angle, p.z);
}

void RoadShapePipe::get_position_at_time(godot::Vector3 &out_pos, const godot::Vector2& in_t) const
{
	RoadTransform road_root_transform;
	owning_segment->curve_matrix->sample(road_root_transform, in_t[1]);

	const float mod_t = (in_t[0] + 1.0f) * 0.5f;

	float mod_vertical_offset = 0.0f;

	for (int i = 0; i < num_modulations; i++)
	{
		const float mod_affector = road_modulations[i].modulation_effect->sample(in_t.y);
		mod_vertical_offset += road_modulations[i].modulation_height->sample(mod_t) * mod_affector;
	}

	const godot::Vector3 pos = godot::Vector3(deterministic_fp::cosf((in_t[0] - 0.5f) * PI), deterministic_fp::sinf((in_t[0] - 0.5f) * PI), 0.0f);
	const godot::Vector3 dir = pos.normalized();
	const godot::Vector3 road_point = pos + dir * mod_vertical_offset;
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
	const godot::Vector2 dir = godot::Vector2(in_pos[0], in_pos[1]).normalized();
	float tx = deterministic_fp::atan2f(-dir[0], -dir[1]) * ONE_DIV_BY_PI;
	tx = 0.5f - tx;
	if (tx < -1.0f)
	{
		tx += 2.0f;
	};
	if (tx > 1.0f)
	{
		tx -= 2.0f;
	};
	out_t = godot::Vector2(-tx, in_pos[2]);
};

void RoadShapeCylinder::get_position_at_time(godot::Vector3 &out_pos, const godot::Vector2& in_t) const
{
	RoadTransform road_root_transform;
	owning_segment->curve_matrix->sample(road_root_transform, in_t[1]);

	const float mod_t = 1.0f - (in_t[0] + 1.0f) * 0.5f;

	float mod_vertical_offset = 0.0f;

	for (int i = 0; i < num_modulations; i++)
	{
		const float mod_affector = road_modulations[i].modulation_effect->sample(in_t.y);
		mod_vertical_offset += road_modulations[i].modulation_height->sample(mod_t) * mod_affector;
	}

	const godot::Vector3 pos = godot::Vector3(deterministic_fp::sinf((in_t.x - 0.5f) * PI), deterministic_fp::cosf((in_t.x - 0.5f) * PI), 0.0f);
	const godot::Vector3 dir = pos.normalized();
	const godot::Vector3 road_point = pos + dir * mod_vertical_offset;
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
	const godot::Vector2 dir = godot::Vector2(in_pos[0], in_pos[1]).normalized();
	const float tx = (deterministic_fp::atan2f(dir[0], -dir[1]) * ONE_DIV_BY_PI) / fmaxf(0.001, openness->sample(in_pos[2]));
	out_t = godot::Vector2(tx, in_pos[2]);
};

void RoadShapePipeOpen::get_position_at_time(godot::Vector3 &out_pos, const godot::Vector2& in_t) const
{
	RoadTransform road_root_transform;
	owning_segment->curve_matrix->sample(road_root_transform, in_t[1]);

	const float mod_t = 1.0f - (in_t[0] + 1.0f) * 0.5f;

	float mod_vertical_offset = 0.0f;

	for (int i = 0; i < num_modulations; i++)
	{
		const float mod_affector = road_modulations[i].modulation_effect->sample(in_t.y);
		mod_vertical_offset += road_modulations[i].modulation_height->sample(mod_t) * mod_affector;
	}

	float mod_tx = in_t[0] * openness->sample(in_t[1]);

	const godot::Vector3 pos = godot::Vector3(deterministic_fp::cosf((mod_tx - 0.5f) * PI), deterministic_fp::sinf((mod_tx - 0.5f) * PI), 0.0f);
	const godot::Vector3 dir = pos.normalized();
	const godot::Vector3 road_point = pos + dir * mod_vertical_offset;
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
	const godot::Vector2 dir = godot::Vector2(in_pos[0], in_pos[1]).normalized();
	float tx = deterministic_fp::atan2f(dir[1], -dir[0]) * ONE_DIV_BY_PI;
	tx = 0.5f - tx;
	if (tx < -1.0f)
	{
		tx += 2.0f;
	};
	if (tx > 1.0f)
	{
		tx -= 2.0f;
	};
	tx /= fmaxf(0.001, openness->sample(in_pos[2]));
	out_t = godot::Vector2(-tx, in_pos[2]);
}

void RoadShapeCylinderOpen::get_position_at_time(godot::Vector3 &out_pos, const godot::Vector2& in_t) const
{
	RoadTransform road_root_transform;
	owning_segment->curve_matrix->sample(road_root_transform, in_t[1]);

	const float mod_t = 1.0f - (in_t[0] + 1.0f) * 0.5f;

	float mod_vertical_offset = 0.0f;

	for (int i = 0; i < num_modulations; i++)
	{
		const float mod_affector = road_modulations[i].modulation_effect->sample(in_t.y);
		mod_vertical_offset += road_modulations[i].modulation_height->sample(mod_t) * mod_affector;
	}

	float mod_tx = in_t[0] * openness->sample(in_t[1]);

	const godot::Vector3 pos = godot::Vector3(deterministic_fp::sinf(mod_tx * PI), deterministic_fp::cosf(mod_tx * PI), 0.0f);
	const godot::Vector3 dir = pos.normalized();
	const godot::Vector3 road_point = pos + dir * mod_vertical_offset;
	godot::Transform3D road_shape_transform = T3D_IDENTITY;
	road_shape_transform.origin = road_point * road_root_transform.scale;
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

	const float normal_sign = -(sign_x * sign_y);
	godot::Vector3 normal = normal_sign * pos_right.cross(pos_forward);

	pos_right.normalize();
	normal.normalize();
	pos_forward.normalize();

	out_transform.basis[0] = sign_x * pos_right;
	out_transform.basis[1] = normal;
	out_transform.basis[2] = sign_y * pos_forward;
	out_transform.origin = base_pos;
}
