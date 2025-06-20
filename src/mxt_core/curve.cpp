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

//void RoadTransformCurve::sample(godot::Transform3D &out_transform, float in_t) const
//{
//	if (num_keyframes == 0)
//	{
//		return;
//	};
//	if (num_keyframes == 1)
//	{
//		get_keyframe_value(out_transform, 0);
//		return;
//	};
//	if (in_t <= keyframes[0].time)
//	{
//		get_keyframe_value(out_transform, 0);
//		return;
//	};
//	if (in_t >= keyframes[num_keyframes - 1].time)
//	{
//		get_keyframe_value(out_transform, num_keyframes - 1);
//		return;
//	};
//	int start_key_index = 0;
//	if (num_keyframes >= 2)
//	{
//		while (in_t > keyframes[start_key_index + 1].time)
//		{
//			start_key_index += 1;
//		};
//	};
//	float dist = keyframes[start_key_index + 1].time - keyframes[start_key_index].time;
//	if (dist == 0)
//	{
//		get_keyframe_value(out_transform, start_key_index + 1);
//		return;
//	};
//
//	in_t = remap_float(in_t, keyframes[start_key_index].time, keyframes[start_key_index + 1].time, 0.0f, 1.0f);
//
//	dist *= 0.33333333f;
//	float omt = (1.0f - in_t);
//	float omt2 = omt * omt;
//	float omt3 = omt2 * omt;
//	float t2 = in_t * in_t;
//	float t3 = t2 * in_t;
//
//	__m128 omt3_vec = _mm_set1_ps(omt3);
//	__m128 omt2t_vec = _mm_set1_ps(omt2 * in_t * 3.0f);
//	__m128 omtt2_vec = _mm_set1_ps(omt * t2 * 3.0f);
//	__m128 t3_vec = _mm_set1_ps(t3);
//
//	__m128 sampled_values [4];
//
//	for (int i = 0; i < 16; i += 4)
//	{
//		__m128 p1 = _mm_loadu_ps(&keyframes[start_key_index].value[i]);
//		__m128 p2 = _mm_loadu_ps(&keyframes[start_key_index + 1].value[i]);
//		__m128 tangent_out = _mm_loadu_ps(&keyframes[start_key_index].tangent_out[i]);
//		__m128 tangent_in = _mm_loadu_ps(&keyframes[start_key_index + 1].tangent_in[i]);
//
//		__m128 p1_handle = _mm_add_ps(p1, _mm_mul_ps(_mm_set1_ps(dist), tangent_out));
//		__m128 p2_handle = _mm_sub_ps(p2, _mm_mul_ps(_mm_set1_ps(dist), tangent_in));
//
//		sampled_values[i / 4] = _mm_add_ps(
//			_mm_add_ps(
//				_mm_mul_ps(p1, omt3_vec),
//				_mm_mul_ps(p1_handle, omt2t_vec)),
//			_mm_add_ps(
//				_mm_mul_ps(p2_handle, omtt2_vec),
//				_mm_mul_ps(p2, t3_vec))
//		);
//	}
//
//	float sampled_curve_values[16];
//	for (int i = 0; i < 4; ++i)
//	{
//		_mm_storeu_ps(&sampled_curve_values[i * 4], sampled_values[i]);
//	}
//
//	out_transform.basis.set(sampled_curve_values[3], sampled_curve_values[6], sampled_curve_values[9],
//		sampled_curve_values[4], sampled_curve_values[7], sampled_curve_values[10],
//		sampled_curve_values[5], sampled_curve_values[8], sampled_curve_values[11]);
//	out_transform.origin.x = sampled_curve_values[0];
//	out_transform.origin.y = sampled_curve_values[1];
//	out_transform.origin.z = sampled_curve_values[2];
//	out_transform.basis.scale_local(godot::Vector3(sampled_curve_values[12], sampled_curve_values[13], sampled_curve_values[14]));
//}

void RoadTransformCurve::sample(godot::Transform3D &out, float in_t) const {
	if (num_keyframes == 0) {
		return;
	}
	if (num_keyframes == 1) {
		const float *v0 = values;
		out.basis.set(
			v0[3],  v0[6],  v0[9],
			v0[4],  v0[7], v0[10],
			v0[5],  v0[8], v0[11]
		);
		out.origin.x = v0[0];
		out.origin.y = v0[1];
		out.origin.z = v0[2];
		out.basis.scale_local(
			godot::Vector3(v0[12], v0[13], v0[14])
		);
		return;
	}

	// clamp
	if (in_t <= times[0]) {
		in_t = times[0];
	} else if (in_t >= times[num_keyframes - 1]) {
		in_t = times[num_keyframes - 1];
	}

	// find segment
	int k = 0;
	while (k + 1 < num_keyframes && in_t > times[k + 1]) {
		++k;
	}
	int k1 = k + 1;
	float t0 = times[k];
	float t1 = times[k1];
	float dt = (t1 > t0) ? (t1 - t0) : 1.0f;
	float u  = remap_float(in_t, t0, t1, 0.0f, 1.0f);

	float omt  = 1.0f - u;
	float omt2 = omt * omt;
	float omt3 = omt2 * omt;
	float t2   = u * u;
	float t3   = t2 * u;
	float dist = dt * (1.0f / 3.0f);

	__m128 omt3v   = _mm_set1_ps(omt3);
	__m128 omt2tv  = _mm_set1_ps(omt2 * u * 3.0f);
	__m128 omtt2v  = _mm_set1_ps(omt * t2 * 3.0f);
	__m128 t3v     = _mm_set1_ps(t3);
	__m128 distv   = _mm_set1_ps(dist);

	float sampled[16];

	for (int chunk = 0; chunk < 4; ++chunk) {
		int base = chunk * 4;
		const float *P0   = values      + k  * 16 + base;
		const float *P1   = values      + k1 * 16 + base;
		const float *Tout = tangent_out + k  * 16 + base;
		const float *Tin  = tangent_in  + k1 * 16 + base;

		__m128 p0 = _mm_load_ps(P0);
		__m128 p1 = _mm_load_ps(P1);
		__m128 to = _mm_load_ps(Tout);
		__m128 ti = _mm_load_ps(Tin);

		__m128 h0 = _mm_add_ps(p0, _mm_mul_ps(distv, to));
		__m128 h1 = _mm_sub_ps(p1, _mm_mul_ps(distv, ti));

		__m128 r = _mm_add_ps(
			_mm_add_ps(_mm_mul_ps(p0,    omt3v), _mm_mul_ps(h0, omt2tv)),
			_mm_add_ps(_mm_mul_ps(h1, omtt2v),   _mm_mul_ps(p1,   t3v))
		);

		_mm_store_ps(sampled + base, r);
	}

	out.basis.set(
		sampled[3],  sampled[6],  sampled[9],
		sampled[4],  sampled[7], sampled[10],
		sampled[5],  sampled[8], sampled[11]
	);
	out.origin.x = sampled[0];
	out.origin.y = sampled[1];
	out.origin.z = sampled[2];
	out.basis.scale_local(
		godot::Vector3(sampled[12], sampled[13], sampled[14])
	);
}