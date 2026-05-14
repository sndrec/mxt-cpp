#include <float.h>
#include <math.h>
#include "mxt_core/math_utils.h"
#include "track/track_segment.h"

struct RoundedRectBoundaryDerivatives
{
	SimVec2 pos;
	SimVec2 d_theta;
	SimVec2 d_w;
	SimVec2 d_h;
	SimVec2 d_r;
};

static inline SimVec3 _mul_components(
	const SimVec3 &a,
	const SimVec3 &b)
{
	return SimVec3(a.x * b.x, a.y * b.y, a.z * b.z);
}

static inline float _sign_no_zero(float value)
{
	return (value < 0.0f) ? -1.0f : 1.0f;
}

static inline SimVec3 _normalized_or_zero(const SimVec3 &value)
{
	const float len2 = value.length_squared();
	if (len2 <= 0.000001f)
	{
		return SimVec3();
	}
	return value * (1.0f / sqrtf(len2));
}

static inline SimVec3 _normalized_or(const SimVec3 &value, const SimVec3 &fallback)
{
	SimVec3 out = _normalized_or_zero(value);
	return out.length_squared() > 0.0f ? out : fallback;
}

static inline void _sample_curve_pair(
	const Curve *curve,
	float in_t,
	float default_value,
	float *value_out,
	float *derivative_out)
{
	if (curve == nullptr)
	{
		*value_out = default_value;
		*derivative_out = 0.0f;
		return;
	}
	curve->sample_with_derivative(in_t, value_out, derivative_out);
}

static inline void _sample_modulation_terms(
	const RoadShape *shape,
	const SimVec2 &in_t,
	float base_value,
	float *value_out,
	float *dx_out,
	float *dy_out)
{
	const float mod_t = 0.5f * (1.0f - in_t.x);
	float value = base_value;
	float dx = 0.0f;
	float dy = 0.0f;

	for (int i = 0; i < shape->num_modulations; ++i)
	{
		float height = 0.0f;
		float height_dt = 0.0f;
		float effect = 0.0f;
		float effect_dt = 0.0f;

		shape->road_modulations[i].modulation_height->sample_with_derivative(
			mod_t,
			&height,
			&height_dt);
		shape->road_modulations[i].modulation_effect->sample_with_derivative(
			in_t.y,
			&effect,
			&effect_dt);

		value += height * effect;
		dx += (-0.5f * height_dt) * effect;
		dy += height * effect_dt;
	}

	*value_out = value;
	*dx_out = dx;
	*dy_out = dy;
}

static inline void _sample_pipe_surface(
	SimVec3 &out_pos,
	SimVec3 &out_dpos_dx,
	SimVec3 &out_dpos_dy,
	const SimVec2 &in_t,
	const RoadShape *shape,
	float angle,
	float angle_dx,
	float angle_dy,
	bool cylinder_axes)
{
	float radius = 0.0f;
	float radius_dx = 0.0f;
	float radius_dy = 0.0f;

	_sample_modulation_terms(shape, in_t, 1.0f, &radius, &radius_dx, &radius_dy);

	SimVec3 dir;
	SimVec3 dir_dangle;
	if (cylinder_axes)
	{
		dir = SimVec3(
			deterministic_fp::sinf(angle),
			deterministic_fp::cosf(angle),
			0.0f);
		dir_dangle = SimVec3(
			deterministic_fp::cosf(angle),
			-deterministic_fp::sinf(angle),
			0.0f);
	}
	else
	{
		dir = SimVec3(
			deterministic_fp::cosf(angle),
			deterministic_fp::sinf(angle),
			0.0f);
		dir_dangle = SimVec3(
			-deterministic_fp::sinf(angle),
			deterministic_fp::cosf(angle),
			0.0f);
	}

	out_pos = dir * radius;
	out_dpos_dx = dir_dangle * (angle_dx * radius) + dir * radius_dx;
	out_dpos_dy = dir_dangle * (angle_dy * radius) + dir * radius_dy;
}

static inline void _compute_clamped_radius_derivatives(
	float w,
	float h,
	float r,
	float *radius_out,
	float *dr_dw_out,
	float *dr_dh_out,
	float *dr_dr_out)
{
	const float w2 = 0.5f * w;
	const float h2 = 0.5f * h;
	const float limit = fminf(w2, h2);

	if (r <= 0.0f)
	{
		*radius_out = 0.0f;
		*dr_dw_out = 0.0f;
		*dr_dh_out = 0.0f;
		*dr_dr_out = 0.0f;
		return;
	}
	if (r < limit)
	{
		*radius_out = r;
		*dr_dw_out = 0.0f;
		*dr_dh_out = 0.0f;
		*dr_dr_out = 1.0f;
		return;
	}
	if (w2 <= h2)
	{
		*radius_out = w2;
		*dr_dw_out = 0.5f;
		*dr_dh_out = 0.0f;
		*dr_dr_out = 0.0f;
		return;
	}
	*radius_out = h2;
	*dr_dw_out = 0.0f;
	*dr_dh_out = 0.5f;
	*dr_dr_out = 0.0f;
}

static inline void _sample_rounded_rect_boundary(
	RoundedRectBoundaryDerivatives &out,
	float theta,
	float w,
	float h,
	float r)
{
	enum
	{
		BRANCH_VERTICAL = 0,
		BRANCH_HORIZONTAL = 1,
		BRANCH_CORNER_NEG = 2,
		BRANCH_CORNER_POS = 3,
	};

	const float eps = 1.0e-6f;
	const float dir_x = deterministic_fp::sinf(theta);
	const float dir_y = deterministic_fp::cosf(theta);
	const float dir_dx = deterministic_fp::cosf(theta);
	const float dir_dy = -deterministic_fp::sinf(theta);
	const float abs_dx = fabsf(dir_x);
	const float abs_dy = fabsf(dir_y);
	const float abs_dx_dtheta = (abs_dx <= eps) ? 0.0f : (_sign_no_zero(dir_x) * dir_dx);
	const float abs_dy_dtheta = (abs_dy <= eps) ? 0.0f : (_sign_no_zero(dir_y) * dir_dy);
	const float w2 = 0.5f * w;
	const float h2 = 0.5f * h;

	float radius_clamped = 0.0f;
	float dr_dw = 0.0f;
	float dr_dh = 0.0f;
	float dr_dr = 0.0f;
	float inner_x;
	float inner_y;
	float inner_x_dw;
	float inner_x_dh;
	float inner_x_dr;
	float inner_y_dw;
	float inner_y_dh;
	float inner_y_dr;
	float best_t = FLT_MAX;
	int branch = BRANCH_VERTICAL;

	_compute_clamped_radius_derivatives(
		w,
		h,
		r,
		&radius_clamped,
		&dr_dw,
		&dr_dh,
		&dr_dr);

	inner_x = w2 - radius_clamped;
	inner_y = h2 - radius_clamped;
	inner_x_dw = 0.5f - dr_dw;
	inner_x_dh = -dr_dh;
	inner_x_dr = -dr_dr;
	inner_y_dw = -dr_dw;
	inner_y_dh = 0.5f - dr_dh;
	inner_y_dr = -dr_dr;

	if (abs_dx > eps)
	{
		const float t_v = w2 / abs_dx;
		if (t_v * abs_dy <= inner_y + eps)
		{
			best_t = t_v;
			branch = BRANCH_VERTICAL;
		}
	}

	if (abs_dy > eps)
	{
		const float t_h = h2 / abs_dy;
		if ((t_h * abs_dx <= inner_x + eps) && (t_h < best_t))
		{
			best_t = t_h;
			branch = BRANCH_HORIZONTAL;
		}
	}

	if (radius_clamped > eps)
	{
		const float m = abs_dx * inner_x + abs_dy * inner_y;
		const float c = inner_x * inner_x + inner_y * inner_y - radius_clamped * radius_clamped;
		const float disc = m * m - c;

		if (disc >= 0.0f)
		{
			const float sqrt_disc = sqrtf(disc);
			const float roots[2] = {m - sqrt_disc, m + sqrt_disc};

			for (int i = 0; i < 2; ++i)
			{
				const float root = roots[i];
				if (root <= 0.0f)
				{
					continue;
				}
				if ((root * abs_dx < inner_x - eps) || (root * abs_dy < inner_y - eps))
				{
					continue;
				}
				if (root < best_t)
				{
					best_t = root;
					branch = (i == 0) ? BRANCH_CORNER_NEG : BRANCH_CORNER_POS;
				}
			}
		}
	}

	if (best_t == FLT_MAX)
	{
		if (w2 <= h2 && abs_dx > eps)
		{
			best_t = w2 / abs_dx;
			branch = BRANCH_VERTICAL;
		}
		else if (abs_dy > eps)
		{
			best_t = h2 / abs_dy;
			branch = BRANCH_HORIZONTAL;
		}
		else
		{
			best_t = fminf(w2, h2);
			branch = BRANCH_VERTICAL;
		}
	}

	float dt_dtheta = 0.0f;
	float dt_dw = 0.0f;
	float dt_dh = 0.0f;
	float dt_dr = 0.0f;

	if (branch == BRANCH_VERTICAL)
	{
		if (abs_dx > eps)
		{
			dt_dtheta = -(w2 * abs_dx_dtheta) / (abs_dx * abs_dx);
			dt_dw = 0.5f / abs_dx;
		}
	}
	else if (branch == BRANCH_HORIZONTAL)
	{
		if (abs_dy > eps)
		{
			dt_dtheta = -(h2 * abs_dy_dtheta) / (abs_dy * abs_dy);
			dt_dh = 0.5f / abs_dy;
		}
	}
	else
	{
		const float m = abs_dx * inner_x + abs_dy * inner_y;
		const float c = inner_x * inner_x + inner_y * inner_y - radius_clamped * radius_clamped;
		const float disc = fmaxf(m * m - c, 0.0f);
		const float sqrt_disc = sqrtf(disc);
		const float sqrt_safe = fmaxf(sqrt_disc, eps);
		const float root_sign = (branch == BRANCH_CORNER_NEG) ? -1.0f : 1.0f;

		const float dm_dtheta = abs_dx_dtheta * inner_x + abs_dy_dtheta * inner_y;
		const float dm_dw = abs_dx * inner_x_dw + abs_dy * inner_y_dw;
		const float dm_dh = abs_dx * inner_x_dh + abs_dy * inner_y_dh;
		const float dm_dr = abs_dx * inner_x_dr + abs_dy * inner_y_dr;

		const float dc_dw =
			2.0f * inner_x * inner_x_dw +
			2.0f * inner_y * inner_y_dw -
			2.0f * radius_clamped * dr_dw;
		const float dc_dh =
			2.0f * inner_x * inner_x_dh +
			2.0f * inner_y * inner_y_dh -
			2.0f * radius_clamped * dr_dh;
		const float dc_dr =
			2.0f * inner_x * inner_x_dr +
			2.0f * inner_y * inner_y_dr -
			2.0f * radius_clamped * dr_dr;

		const float ddisc_dtheta = 2.0f * m * dm_dtheta;
		const float ddisc_dw = 2.0f * m * dm_dw - dc_dw;
		const float ddisc_dh = 2.0f * m * dm_dh - dc_dh;
		const float ddisc_dr = 2.0f * m * dm_dr - dc_dr;

		dt_dtheta = dm_dtheta + root_sign * (0.5f * ddisc_dtheta / sqrt_safe);
		dt_dw = dm_dw + root_sign * (0.5f * ddisc_dw / sqrt_safe);
		dt_dh = dm_dh + root_sign * (0.5f * ddisc_dh / sqrt_safe);
		dt_dr = dm_dr + root_sign * (0.5f * ddisc_dr / sqrt_safe);
	}

	const SimVec2 dir(dir_x, dir_y);
	const SimVec2 dir_dtheta(dir_dx, dir_dy);

	out.pos = dir * best_t;
	out.d_theta = dir_dtheta * best_t + dir * dt_dtheta;
	out.d_w = dir * dt_dw;
	out.d_h = dir * dt_dh;
	out.d_r = dir * dt_dr;
}

void RoadShape::find_t_from_relative_pos(SimVec2 &out_t, const SimVec3& in_pos) const
{
	out_t = SimVec2(in_pos[0], in_pos[2]);
};

void RoadShape::get_position_at_time(SimVec3 &out_pos, const SimVec2& in_t) const
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
	const SimVec3 local_pos(in_t.x * road_root.scale.x, vertical_offset * road_root.scale.y, 0.0f);
	out_pos = road_root.t3d.xform(local_pos);
}

void RoadShape::get_local_surface_at_time(
	SimVec3 &out_pos,
	SimVec3 &out_dpos_dx,
	SimVec3 &out_dpos_dy,
	const SimVec2& in_t) const
{
	float vertical_offset = 0.0f;
	float vertical_offset_dx = 0.0f;
	float vertical_offset_dy = 0.0f;

	_sample_modulation_terms(
		this,
		in_t,
		0.0f,
		&vertical_offset,
		&vertical_offset_dx,
		&vertical_offset_dy);

	out_pos = SimVec3(in_t.x, vertical_offset, 0.0f);
	out_dpos_dx = SimVec3(1.0f, vertical_offset_dx, 0.0f);
	out_dpos_dy = SimVec3(0.0f, vertical_offset_dy, 0.0f);
}

void RoadShapePipe::find_t_from_relative_pos(SimVec2 &out_t, const SimVec3& p) const
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
        out_t = SimVec2(phi, p.z);
}

void RoadShapePipe::get_position_at_time(SimVec3 &out_pos, const SimVec2& in_t) const
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
        const SimVec3 pos = SimVec3(
                deterministic_fp::cosf(tx_angle),
                deterministic_fp::sinf(tx_angle),
                0.0f);
	const SimVec3 dir = pos.normalized();
	const SimVec3 road_point = dir * mod_vertical_offset;
	const SimTransform road_shape_transform = SimTransform(SimBasis(), road_point * road_root_transform.scale);
	const SimTransform final_transform = road_root_transform.t3d * road_shape_transform;

	out_pos = final_transform.origin;
};

void RoadShapePipe::get_local_surface_at_time(
	SimVec3 &out_pos,
	SimVec3 &out_dpos_dx,
	SimVec3 &out_dpos_dy,
	const SimVec2& in_t) const
{
	_sample_pipe_surface(
		out_pos,
		out_dpos_dx,
		out_dpos_dy,
		in_t,
		this,
		(in_t.x - 0.5f) * PI,
		PI,
		0.0f,
		false);
}

void RoadShapeCylinder::find_t_from_relative_pos(SimVec2 &out_t, const SimVec3& in_pos) const
{
        const float theta = deterministic_fp::atan2f(in_pos.x, in_pos.y);
        out_t = SimVec2(theta * ONE_DIV_BY_PI, in_pos.z);
};

void RoadShapeCylinder::get_position_at_time(SimVec3 &out_pos, const SimVec2& in_t) const
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
        const SimVec3 pos = SimVec3(
                deterministic_fp::sinf(theta),
                deterministic_fp::cosf(theta),
                0.0f);
	const SimVec3 dir = pos.normalized();
	const SimVec3 road_point = dir * mod_vertical_offset;
	SimTransform road_shape_transform = SimTransform();
	road_shape_transform.origin = road_point * road_root_transform.scale;
	const SimTransform final_transform = road_root_transform.t3d * road_shape_transform;

	out_pos = final_transform.origin;
};

void RoadShapeCylinder::get_local_surface_at_time(
	SimVec3 &out_pos,
	SimVec3 &out_dpos_dx,
	SimVec3 &out_dpos_dy,
	const SimVec2& in_t) const
{
	_sample_pipe_surface(
		out_pos,
		out_dpos_dx,
		out_dpos_dy,
		in_t,
		this,
		in_t.x * PI,
		PI,
		0.0f,
		true);
}

void RoadShapePipeOpen::find_t_from_relative_pos(SimVec2 &out_t, const SimVec3& in_pos) const
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
        out_t = SimVec2(phi, in_pos[2]);
};

void RoadShapePipeOpen::get_position_at_time(SimVec3 &out_pos, const SimVec2& in_t) const
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
        const SimVec3 pos = SimVec3(
                deterministic_fp::cosf(tx_angle),
                deterministic_fp::sinf(tx_angle),
                0.0f);
	const SimVec3 dir = pos.normalized();
	const SimVec3 road_point = dir * mod_vertical_offset;
	SimTransform road_shape_transform = SimTransform();
	road_shape_transform.origin = road_point * road_root_transform.scale;
	const SimTransform final_transform = road_root_transform.t3d * road_shape_transform;

	out_pos = final_transform.origin;
};

void RoadShapePipeOpen::get_local_surface_at_time(
	SimVec3 &out_pos,
	SimVec3 &out_dpos_dx,
	SimVec3 &out_dpos_dy,
	const SimVec2& in_t) const
{
	float open_value = 1.0f;
	float open_derivative = 0.0f;
	_sample_curve_pair(openness, in_t.y, 1.0f, &open_value, &open_derivative);

	_sample_pipe_surface(
		out_pos,
		out_dpos_dx,
		out_dpos_dy,
		in_t,
		this,
		((in_t.x * open_value) - 0.5f) * PI,
		PI * open_value,
		PI * in_t.x * open_derivative,
		false);
}

void RoadShapeCylinderOpen::find_t_from_relative_pos(SimVec2 &out_t, const SimVec3& in_pos) const
{
        float tx = deterministic_fp::atan2f(in_pos.x, in_pos.y) * ONE_DIV_BY_PI;
        tx /= fmaxf(0.001f, openness->sample(in_pos[2]));
        out_t = SimVec2(tx, in_pos[2]);
}

void RoadShapeCylinderOpen::get_position_at_time(SimVec3 &out_pos, const SimVec2& in_t) const
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
        const SimVec3 pos = SimVec3(
                deterministic_fp::sinf(theta),
                deterministic_fp::cosf(theta),
                0.0f);
	const SimVec3 dir = pos.normalized();
	const SimVec3 road_point = dir * mod_vertical_offset;
	SimTransform road_shape_transform = SimTransform();
	road_shape_transform.origin = road_point * road_root_transform.scale;
	const SimTransform final_transform = road_root_transform.t3d * road_shape_transform;

        out_pos = final_transform.origin;
};

void RoadShapeCylinderOpen::get_local_surface_at_time(
	SimVec3 &out_pos,
	SimVec3 &out_dpos_dx,
	SimVec3 &out_dpos_dy,
	const SimVec2& in_t) const
{
	float open_value = 1.0f;
	float open_derivative = 0.0f;
	_sample_curve_pair(openness, in_t.y, 1.0f, &open_value, &open_derivative);

	_sample_pipe_surface(
		out_pos,
		out_dpos_dx,
		out_dpos_dy,
		in_t,
		this,
		(in_t.x * open_value) * PI,
		PI * open_value,
		PI * in_t.x * open_derivative,
		true);
}

static inline SimVec2 _closest_point_on_rounded_rect(const SimVec2 &p, float w, float h, float r)
{
        const float w2 = 0.5f * w;
        const float h2 = 0.5f * h;
        const float radius = fminf(fmaxf(r, 0.0f), fminf(w2, h2));
        const float inner_x = w2 - radius;
        const float inner_y = h2 - radius;

        SimVec2 pp(fabsf(p.x), fabsf(p.y));
        SimVec2 result;

        if (pp.x > inner_x && pp.y > inner_y)
        {
                SimVec2 corner_center(inner_x, inner_y);
                SimVec2 off = pp - corner_center;
                const float len = off.length();
                if (len > 1.0e-6f)
                        off *= radius / len;
                else
                        off = SimVec2(radius, 0.0f);
                result = corner_center + off;
        }
        else if (pp.x > inner_x)
        {
                result = SimVec2(w2, pp.y);
        }
        else if (pp.y > inner_y)
        {
                result = SimVec2(pp.x, h2);
        }
        else
        {
                const float dx = w2 - pp.x;
                const float dy = h2 - pp.y;
                if (dx < dy)
                        result = SimVec2(w2, pp.y);
                else
                        result = SimVec2(pp.x, h2);
        }

        result.x = copysignf(result.x, p.x);
        result.y = copysignf(result.y, p.y);
        return result;
}

void RoadShapeRoundedRect::find_t_from_relative_pos(SimVec2 &out_t, const SimVec3& p) const
{
        const float w = width->sample(p.z);
        const float h = height->sample(p.z);
        const float r = radius->sample(p.z);

        const SimVec2 closest = _closest_point_on_rounded_rect(SimVec2(p.x, p.y), w, h, r);

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
        out_t = SimVec2(tx, p.z);
};

static inline float _rounded_rect_length(const SimVec2 &dir, float w, float h, float r)
{
	const float w2 = 0.5f * w;
	const float h2 = 0.5f * h;
	const float radius = fminf(fmaxf(r, 0.0f), fminf(w2, h2));

	const float abs_dx = fabsf(dir.x);
	const float abs_dy = fabsf(dir.y);

	float min_t = FLT_MAX;

	if (abs_dx > 1.0e-6f) {
		const float t_v = w2 / abs_dx;
		if (t_v * abs_dy <= h2 - radius + 1.0e-6f)
			min_t = t_v;
	}

	if (abs_dy > 1.0e-6f) {
		const float t_h = h2 / abs_dy;
		if (t_h * abs_dx <= w2 - radius + 1.0e-6f)
			min_t = fminf(min_t, t_h);
	}

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

	if (min_t == FLT_MAX)
		min_t = fminf(w2, h2);

	SimVec2 p = dir * min_t;
	return p.length();
}

void RoadShapeRoundedRect::get_position_at_time(SimVec3 &out_pos, const SimVec2& in_t) const
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
        const SimVec2 dir(deterministic_fp::sinf(theta), deterministic_fp::cosf(theta));

        float length = _rounded_rect_length(dir, w, h, r);
        length *= mod_vertical_offset;
        const SimVec2 final = dir * length;

        SimTransform road_shape_transform = SimTransform();
        road_shape_transform.origin = SimVec3(final.x, final.y, 0.0f) * road_root_transform.scale;
        const SimTransform final_transform = road_root_transform.t3d * road_shape_transform;

        out_pos = final_transform.origin;
};

void RoadShapeRoundedRect::get_local_surface_at_time(
	SimVec3 &out_pos,
	SimVec3 &out_dpos_dx,
	SimVec3 &out_dpos_dy,
	const SimVec2& in_t) const
{
	float radial_scale = 0.0f;
	float radial_scale_dx = 0.0f;
	float radial_scale_dy = 0.0f;
	float width_value = 0.0f;
	float width_derivative = 0.0f;
	float height_value = 0.0f;
	float height_derivative = 0.0f;
	float radius_value = 0.0f;
	float radius_derivative = 0.0f;
	RoundedRectBoundaryDerivatives boundary;
	const float theta = (1.0f - in_t.x) * PI;
	const float theta_dx = -PI;

	_sample_modulation_terms(
		this,
		in_t,
		1.0f,
		&radial_scale,
		&radial_scale_dx,
		&radial_scale_dy);
	width->sample_with_derivative(in_t.y, &width_value, &width_derivative);
	height->sample_with_derivative(in_t.y, &height_value, &height_derivative);
	radius->sample_with_derivative(in_t.y, &radius_value, &radius_derivative);
	_sample_rounded_rect_boundary(boundary, theta, width_value, height_value, radius_value);

	const SimVec2 local_pos_2d = boundary.pos * radial_scale;
	const SimVec2 local_dx_2d =
		boundary.d_theta * (theta_dx * radial_scale) +
		boundary.pos * radial_scale_dx;
	const SimVec2 local_dy_2d =
		(boundary.d_w * width_derivative +
		 boundary.d_h * height_derivative +
		 boundary.d_r * radius_derivative) * radial_scale +
		boundary.pos * radial_scale_dy;

	out_pos = SimVec3(local_pos_2d.x, local_pos_2d.y, 0.0f);
	out_dpos_dx = SimVec3(local_dx_2d.x, local_dx_2d.y, 0.0f);
	out_dpos_dy = SimVec3(local_dy_2d.x, local_dy_2d.y, 0.0f);
}

void RoadShapeRoundedRectOpen::find_t_from_relative_pos(SimVec2 &out_t, const SimVec3& p) const
{
        const float w = width->sample(p.z);
        const float h = height->sample(p.z);
        const float r = radius->sample(p.z);

        const SimVec2 closest = _closest_point_on_rounded_rect(SimVec2(p.x, p.y), w, h, r);

        float theta = deterministic_fp::atan2f(closest.x, closest.y);
        if (theta < 0.0f)
        {
                theta += PI * 2.0f;
        }
        float mod_tx = 1.0f - theta * ONE_DIV_BY_PI;
        const float rot = (open_rotation) ? open_rotation->sample(p.z) : 0.0f;
        mod_tx -= rot;
        if (mod_tx < -1.0f) mod_tx += 2.0f;
        if (mod_tx > 1.0f) mod_tx -= 2.0f;
        float openness_v = fmaxf(0.001f, openness->sample(p.z));
        float tx = mod_tx / openness_v;
        out_t = SimVec2(tx, p.z);
};

void RoadShapeRoundedRectOpen::get_position_at_time(SimVec3 &out_pos, const SimVec2& in_t) const
{
        RoadTransform road_root_transform;
        owning_segment->curve_matrix->sample(road_root_transform, in_t[1]);

        float mod_tx = in_t[0] * openness->sample(in_t[1]);
        const float rot = (open_rotation) ? open_rotation->sample(in_t[1]) : 0.0f;
        mod_tx += rot;
        if (mod_tx < -1.0f) mod_tx += 2.0f;
        if (mod_tx > 1.0f) mod_tx -= 2.0f;
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
        const SimVec2 dir(deterministic_fp::sinf(theta), deterministic_fp::cosf(theta));

        float length = _rounded_rect_length(dir, w, h, r);
        length *= mod_vertical_offset;
        const SimVec2 final = dir * length;

        SimTransform road_shape_transform = SimTransform();
        road_shape_transform.origin = SimVec3(final.x, final.y, 0.0f) * road_root_transform.scale;
        const SimTransform final_transform = road_root_transform.t3d * road_shape_transform;

        out_pos = final_transform.origin;
};

void RoadShapeRoundedRectOpen::get_local_surface_at_time(
	SimVec3 &out_pos,
	SimVec3 &out_dpos_dx,
	SimVec3 &out_dpos_dy,
	const SimVec2& in_t) const
{
	float radial_scale = 0.0f;
	float radial_scale_dx = 0.0f;
	float radial_scale_dy = 0.0f;
	float width_value = 0.0f;
	float width_derivative = 0.0f;
	float height_value = 0.0f;
	float height_derivative = 0.0f;
	float radius_value = 0.0f;
	float radius_derivative = 0.0f;
	float open_value = 1.0f;
	float open_derivative = 0.0f;
	float rotation_value = 0.0f;
	float rotation_derivative = 0.0f;
	float mod_tx;
	RoundedRectBoundaryDerivatives boundary;

	_sample_modulation_terms(
		this,
		in_t,
		1.0f,
		&radial_scale,
		&radial_scale_dx,
		&radial_scale_dy);
	width->sample_with_derivative(in_t.y, &width_value, &width_derivative);
	height->sample_with_derivative(in_t.y, &height_value, &height_derivative);
	radius->sample_with_derivative(in_t.y, &radius_value, &radius_derivative);
	_sample_curve_pair(openness, in_t.y, 1.0f, &open_value, &open_derivative);
	_sample_curve_pair(open_rotation, in_t.y, 0.0f, &rotation_value, &rotation_derivative);

	mod_tx = in_t.x * open_value + rotation_value;
	if (mod_tx < -1.0f)
	{
		mod_tx += 2.0f;
	}
	if (mod_tx > 1.0f)
	{
		mod_tx -= 2.0f;
	}

	const float theta = (1.0f - mod_tx) * PI;
	const float theta_dx = -PI * open_value;
	const float theta_dy = -PI * (in_t.x * open_derivative + rotation_derivative);

	_sample_rounded_rect_boundary(boundary, theta, width_value, height_value, radius_value);

	const SimVec2 local_pos_2d = boundary.pos * radial_scale;
	const SimVec2 local_dx_2d =
		boundary.d_theta * (theta_dx * radial_scale) +
		boundary.pos * radial_scale_dx;
	const SimVec2 local_dy_2d =
		(boundary.d_theta * theta_dy +
		 boundary.d_w * width_derivative +
		 boundary.d_h * height_derivative +
		 boundary.d_r * radius_derivative) * radial_scale +
		boundary.pos * radial_scale_dy;

	out_pos = SimVec3(local_pos_2d.x, local_pos_2d.y, 0.0f);
	out_dpos_dx = SimVec3(local_dx_2d.x, local_dx_2d.y, 0.0f);
	out_dpos_dy = SimVec3(local_dy_2d.x, local_dy_2d.y, 0.0f);
}

void RoadShape::get_oriented_transform_at_time(SimTransform &out_transform, const SimVec2& in_t) const
{
	RoadTransform root;
	RoadTransform root_derivative;
	owning_segment->curve_matrix->sample_with_derivative(root, root_derivative, in_t.y);
	get_oriented_transform_at_time_presampled(out_transform, in_t, root, root_derivative);
}

void RoadShape::get_oriented_transform_at_time_presampled(
	SimTransform &out_transform,
	const SimVec2& in_t,
	const RoadTransform& root,
	const RoadTransform& root_derivative) const
{
	SimVec3 tangent_x;
	SimVec3 tangent_y;
	get_oriented_transform_at_time_presampled(
		out_transform,
		tangent_x,
		tangent_y,
		in_t,
		root,
		root_derivative);
}

void RoadShape::get_oriented_transform_at_time_presampled(
	SimTransform &out_transform,
	SimVec3 &out_tangent_x,
	SimVec3 &out_tangent_y,
	const SimVec2& in_t,
	const RoadTransform& root,
	const RoadTransform& root_derivative) const
{
	SimVec3 local_pos;
	SimVec3 local_dx;
	SimVec3 local_dy;
	SimVec3 scaled_pos;
	SimVec3 scaled_dx;
	SimVec3 scaled_dy;
	SimVec3 tangent_x;
	SimVec3 tangent_y;
	SimVec3 right;
	SimVec3 normal;
	SimVec3 forward;

	get_local_surface_at_time(local_pos, local_dx, local_dy, in_t);

	scaled_pos = _mul_components(local_pos, root.scale);
	scaled_dx = _mul_components(local_dx, root.scale);
	scaled_dy = _mul_components(local_dy, root.scale) + _mul_components(local_pos, root_derivative.scale);

	out_transform.origin = root.t3d.xform(scaled_pos);
	tangent_x = root.t3d.basis.xform(scaled_dx);
	tangent_y =
		root_derivative.t3d.origin +
		root_derivative.t3d.basis.xform(scaled_pos) +
		root.t3d.basis.xform(scaled_dy);
	out_tangent_x = tangent_x;
	out_tangent_y = tangent_y;

	right = tangent_x.normalized();
	normal = tangent_y.cross(tangent_x).normalized();
	forward = right.cross(normal).normalized();

	out_transform.basis[0] = right;
	out_transform.basis[1] = normal;
	out_transform.basis[2] = forward;
}

void RoadShape::get_oriented_transform_at_time4(SimTransform out_transform[4], const SimVec2 in_t[4]) const
{
	RoadTransform root[4];
	RoadTransform root_derivative[4];
	float ty[4] = { in_t[0].y, in_t[1].y, in_t[2].y, in_t[3].y };
	owning_segment->curve_matrix->sample4_with_derivative(root, root_derivative, ty);

	for (int lane = 0; lane < 4; ++lane) {
		SimVec3 local_pos;
		SimVec3 local_dx;
		SimVec3 local_dy;
		get_local_surface_at_time(local_pos, local_dx, local_dy, in_t[lane]);

		const SimVec3 scaled_pos = _mul_components(local_pos, root[lane].scale);
		const SimVec3 scaled_dx = _mul_components(local_dx, root[lane].scale);
		const SimVec3 scaled_dy =
			_mul_components(local_dy, root[lane].scale) +
			_mul_components(local_pos, root_derivative[lane].scale);

		out_transform[lane].origin = root[lane].t3d.xform(scaled_pos);
		const SimVec3 tangent_x = root[lane].t3d.basis.xform(scaled_dx);
		const SimVec3 tangent_y =
			root_derivative[lane].t3d.origin +
			root_derivative[lane].t3d.basis.xform(scaled_pos) +
			root[lane].t3d.basis.xform(scaled_dy);

		const SimVec3 right = tangent_x.normalized();
		const SimVec3 normal = tangent_y.cross(tangent_x).normalized();
		const SimVec3 forward = right.cross(normal).normalized();

		out_transform[lane].basis[0] = right;
		out_transform[lane].basis[1] = normal;
		out_transform[lane].basis[2] = forward;
	}
}

bool RoadShape::supports_edge_rails() const
{
	switch (shape_type) {
		case ROAD_SHAPE_TYPE::ROAD_SHAPE_FLAT:
		case ROAD_SHAPE_TYPE::ROAD_SHAPE_TUNNEL:
		case ROAD_SHAPE_TYPE::ROAD_SHAPE_CYLINDER_OPEN:
		case ROAD_SHAPE_TYPE::ROAD_SHAPE_PIPE_OPEN:
		case ROAD_SHAPE_TYPE::ROAD_SHAPE_ROUNDED_RECT_OPEN:
			return true;
		default:
			return false;
	}
}

void RoadShape::get_edge_rail_sides(
	TrackEdgeRailSide out_sides[2],
	float road_y,
	const SimVec3& interior_reference,
	const RoadTransform& root,
	const RoadTransform& root_derivative,
	float left_height,
	float right_height) const
{
	const float edge_x[2] = { 1.0f, -1.0f };
	const float edge_height[2] = { left_height, right_height };
	const SimVec3 root_up = _normalized_or_zero(root.t3d.basis[1]);
	const SimVec3 root_forward = _normalized_or_zero(root.t3d.basis[2]);
	for (int i = 0; i < 2; ++i) {
		SimTransform edge_surface;
		SimVec3 edge_tangent_x;
		SimVec3 edge_tangent_y;
		get_oriented_transform_at_time_presampled(
			edge_surface,
			edge_tangent_x,
			edge_tangent_y,
			SimVec2(edge_x[i], road_y),
			root,
			root_derivative);

		SimVec3 up_n = _normalized_or(edge_surface.basis[1], root_up);
		SimVec3 forward_n = _normalized_or(edge_tangent_y, _normalized_or(edge_surface.basis[2], root_forward));
		SimVec3 rail_n = _normalized_or_zero(up_n.cross(forward_n));
		if (rail_n.length_squared() <= 0.0f) {
			rail_n = _normalized_or_zero(interior_reference - edge_surface.origin);
		}
		if (rail_n.length_squared() <= 0.0f) {
			rail_n = _normalized_or(edge_surface.basis[0], root.t3d.basis[0]);
		}
		if (rail_n.dot(interior_reference - edge_surface.origin) < 0.0f) {
			rail_n = -rail_n;
		}

		out_sides[i].pos = edge_surface.origin;
		out_sides[i].rail_n = rail_n;
		out_sides[i].up_n = up_n;
		out_sides[i].forward_n = forward_n;
		out_sides[i].height = edge_height[i];
	}
}
