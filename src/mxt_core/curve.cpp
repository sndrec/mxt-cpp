#include "mxt_core/curve.h"
#include "mxt_core/math_utils.h"
#include "godot_cpp/variant/utility_functions.hpp"
#include <xmmintrin.h>

float Curve::sample(float in_t) const
{
	if (num_keyframes == 0)
	{
		return 0.0;
	};
	if (num_keyframes == 1)
	{
		return keyframes[0].value;
	};
	if (in_t <= keyframes[0].time)
	{
		return keyframes[0].value;
	};
	if (in_t >= keyframes[num_keyframes - 1].time)
	{
		return keyframes[num_keyframes - 1].value;
	};
	int start_key_index = 0;
	if (num_keyframes >= 2)
	{
		while (in_t > keyframes[start_key_index + 1].time)
		{
			start_key_index += 1;
		};
	};
	float p1 = keyframes[start_key_index].value;
	float p2 = keyframes[start_key_index + 1].value;
	float dist = keyframes[start_key_index + 1].time - keyframes[start_key_index].time;
	if (dist == 0)
	{
		return p2;
	};
	in_t = remap_float(in_t, keyframes[start_key_index].time, keyframes[start_key_index + 1].time, 0.0f, 1.0f);
	dist *= 0.33333333f;
	float p1_handle = p1 + dist * keyframes[start_key_index].tangent_out;
	float p2_handle = p2 - dist * keyframes[start_key_index + 1].tangent_in;
	float omt = (1.0f - in_t);
	float omt2 = omt * omt;
	float omt3 = omt2 * omt;
	float t2 = in_t * in_t;
	float t3 = t2 * in_t;
	float dp_dv = p1 * omt3 + p1_handle * omt2 * in_t * 3.0f + p2_handle * omt * t2 * 3.0f + p2 * t3;
	return dp_dv;
};

void RoadTransformCurve::sample(godot::Transform3D &out_transform, float in_t) const
{
	if (num_keyframes == 0)
	{
		return;
	};
	if (num_keyframes == 1)
	{
		get_keyframe_value(out_transform, 0);
		return;
	};
	if (in_t <= keyframes[0].time)
	{
		get_keyframe_value(out_transform, 0);
		return;
	};
	if (in_t >= keyframes[num_keyframes - 1].time)
	{
		get_keyframe_value(out_transform, num_keyframes - 1);
		return;
	};
	int start_key_index = 0;
	if (num_keyframes >= 2)
	{
		while (in_t > keyframes[start_key_index + 1].time)
		{
			start_key_index += 1;
		};
	};
	float dist = keyframes[start_key_index + 1].time - keyframes[start_key_index].time;
	if (dist == 0)
	{
		get_keyframe_value(out_transform, start_key_index + 1);
		return;
	};

	in_t = remap_float(in_t, keyframes[start_key_index].time, keyframes[start_key_index + 1].time, 0.0f, 1.0f);

	dist *= 0.33333333f;
	float omt = (1.0f - in_t);
	float omt2 = omt * omt;
	float omt3 = omt2 * omt;
	float t2 = in_t * in_t;
	float t3 = t2 * in_t;

	__m128 omt3_vec = _mm_set1_ps(omt3);
	__m128 omt2t_vec = _mm_set1_ps(omt2 * in_t * 3.0f);
	__m128 omtt2_vec = _mm_set1_ps(omt * t2 * 3.0f);
	__m128 t3_vec = _mm_set1_ps(t3);

	__m128 sampled_values [4];

	for (int i = 0; i < 16; i += 4)
	{
		__m128 p1 = _mm_loadu_ps(&keyframes[start_key_index].value[i]);
		__m128 p2 = _mm_loadu_ps(&keyframes[start_key_index + 1].value[i]);
		__m128 tangent_out = _mm_loadu_ps(&keyframes[start_key_index].tangent_out[i]);
		__m128 tangent_in = _mm_loadu_ps(&keyframes[start_key_index + 1].tangent_in[i]);

		__m128 p1_handle = _mm_add_ps(p1, _mm_mul_ps(_mm_set1_ps(dist), tangent_out));
		__m128 p2_handle = _mm_sub_ps(p2, _mm_mul_ps(_mm_set1_ps(dist), tangent_in));

		sampled_values[i / 4] = _mm_add_ps(
			_mm_add_ps(
				_mm_mul_ps(p1, omt3_vec),
				_mm_mul_ps(p1_handle, omt2t_vec)),
			_mm_add_ps(
				_mm_mul_ps(p2_handle, omtt2_vec),
				_mm_mul_ps(p2, t3_vec))
		);
	}

	float sampled_curve_values[16];
	for (int i = 0; i < 4; ++i)
	{
		_mm_storeu_ps(&sampled_curve_values[i * 4], sampled_values[i]);
	}

	out_transform.basis.set(sampled_curve_values[3], sampled_curve_values[6], sampled_curve_values[9],
		sampled_curve_values[4], sampled_curve_values[7], sampled_curve_values[10],
		sampled_curve_values[5], sampled_curve_values[8], sampled_curve_values[11]);
	out_transform.origin.x = sampled_curve_values[0];
	out_transform.origin.y = sampled_curve_values[1];
	out_transform.origin.z = sampled_curve_values[2];
	out_transform.basis.scale_local(godot::Vector3(sampled_curve_values[12], sampled_curve_values[13], sampled_curve_values[14]));
}