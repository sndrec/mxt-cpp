#pragma once

#include "godot_cpp/classes/engine.hpp"
#include <cstddef>
#include <new>
#include <immintrin.h>

struct CurveKeyframe
{
	float time; // between 0.0 and 1.0 inclusive
	float value;
	float tangent_in;
	float tangent_out;
};

class Curve
{
public:
	int num_keyframes;
	CurveKeyframe* keyframes;
	Curve(int keyframe_count, CurveKeyframe* in_keyframes)
	{
		keyframes = in_keyframes;
		num_keyframes = keyframe_count;
	}

	float sample(float in_t) const;
};

struct RoadTransformCurveKeyframe
{
	float time; // between 0.0 and 1.0 inclusive
	float value[16];
	float tangent_in[16];
	float tangent_out[16];
};

//class RoadTransformCurve
//{
//public:
//	int num_keyframes;
//	RoadTransformCurveKeyframe* keyframes;
//	RoadTransformCurve(int keyframe_count, RoadTransformCurveKeyframe* in_keyframes)
//	{
//		keyframes = in_keyframes;
//		num_keyframes = keyframe_count;
//	}
//	void sample(godot::Transform3D &out_transform, float in_t) const;
//	void get_keyframe_value(godot::Transform3D &out_transform, int in_keyframe) const
//	{
//		out_transform.basis.set(keyframes[in_keyframe].value[3], keyframes[in_keyframe].value[6], keyframes[in_keyframe].value[9],
//			keyframes[in_keyframe].value[4], keyframes[in_keyframe].value[7], keyframes[in_keyframe].value[10],
//			keyframes[in_keyframe].value[5], keyframes[in_keyframe].value[8], keyframes[in_keyframe].value[11]);
//		out_transform.origin.x = keyframes[in_keyframe].value[0];
//		out_transform.origin.y = keyframes[in_keyframe].value[1];
//		out_transform.origin.z = keyframes[in_keyframe].value[2];
//		out_transform.basis.scale_local(godot::Vector3(keyframes[in_keyframe].value[12], keyframes[in_keyframe].value[13], keyframes[in_keyframe].value[14]));
//	}
//};

struct alignas(16) RoadTransformCurve {
    int num_keyframes;
    float *times, *values, *tangent_in, *tangent_out;
    // precomputed coefficients per segment for cubic Hermite sampling
    float *inv_dt;    // 1/(t1-t0) for each segment
    float *coef_a;    // cubic coefficients a for each component
    float *coef_b;    // cubic coefficients b
    float *coef_c;    // cubic coefficients c
    float *coef_d;    // cubic coefficients d
    RoadTransformCurve(int count): num_keyframes(count) {}

	// fetch raw keyframe into a Transform3D
	void get_keyframe_value(godot::Transform3D &out, int idx) const {
		const float *v = values + idx * 16;
		out.basis.set(
			v[3],  v[6],  v[9],
			v[4],  v[7], v[10],
			v[5],  v[8], v[11]
		);
		out.origin.x = v[0];
		out.origin.y = v[1];
		out.origin.z = v[2];
		out.basis.scale_local(
			godot::Vector3(v[12], v[13], v[14])
		);
	}

        void sample(godot::Transform3D &out, float in_t) const;
        void precompute();
};