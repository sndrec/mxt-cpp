#pragma once

#include "mxt_core/sim_math.h"
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


struct RoadTransform
{
    SimTransform t3d;
    SimVec3 scale;
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
	float sample_derivative(float in_t) const;
	void sample_with_derivative(float in_t, float *value_out, float *derivative_out) const;
};

struct RoadTransformCurveKeyframe
{
	float time; // between 0.0 and 1.0 inclusive
	float value[16];
	float tangent_in[16];
	float tangent_out[16];
};

struct alignas(16) RoadTransformCurve {
    int num_keyframes;
    float *times, *values, *tangent_in, *tangent_out;
    // precomputed coefficients per segment for cubic Hermite sampling
    float *inv_dt;    // 1/(t1-t0) for each segment
    float *coef_a;    // cubic coefficients a for each component
    float *coef_b;    // cubic coefficients b
    float *coef_c;    // cubic coefficients c
    float *coef_d;    // cubic coefficients d
	int last_k;
    RoadTransformCurve(int count): num_keyframes(count) {}

	// fetch raw keyframe into a sim transform
	void get_keyframe_value(RoadTransform &out, int idx) const {
		const float *v = values + idx * 16;
		out.t3d.basis = SimBasis(
			v[3], v[4], v[5],
			v[6], v[7], v[8],
			v[9], v[10], v[11]);
		out.t3d.origin = SimVec3(v[0], v[1], v[2]);
		out.scale = SimVec3(v[12], v[13], v[14]);
	}

        void sample(RoadTransform &out, float in_t);
        void sample_with_derivative(RoadTransform &out, RoadTransform &derivative_out, float in_t);
        void sample4(RoadTransform out[4], const float in_t[4]);
        void sample4_with_derivative(RoadTransform out[4], RoadTransform derivative_out[4], const float in_t[4]);
        void precompute();
};
