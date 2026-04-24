#pragma once

#include "mxt_core/deterministic_fp.hpp"
#include "mxt_core/sim_math.h"
#include <cmath>
#include <cstdint>

constexpr float _A = 1738.0f;

constexpr float _U_TO_KMH = 6.0f;
constexpr float _KMH_TO_U = 1.0f / _U_TO_KMH;
constexpr float _GRAVITY = -120.0f;
constexpr float _TICKS_PER_SECOND = 60.0f;
constexpr float _TICK_DELTA = 1.0f / _TICKS_PER_SECOND;
constexpr float PI = 3.1415926535897932;
constexpr float TAU = 6.2831853071795864;
constexpr float DEG_TO_RAD = PI / 180.0f;
constexpr float RAD_TO_DEG = 180.0f / PI;
constexpr float ONE_DIV_BY_PI = 1.0 / PI;

template <typename T> static int sgn(T val) {
	return (T(0) < val) - (val < T(0));
}

static SimVec3 get_closest_point_to_segment_uncapped(SimVec3 p_point, SimVec3 p_segment_start, SimVec3 p_segment_end) {
	SimVec3 p = p_point - p_segment_start;
	SimVec3 n = p_segment_end - p_segment_start;
	float l2 = n.length_squared();
	if (l2 < 1e-20f) {
		return p_segment_start;
	}

	float d = n.dot(p) / l2;

	return p_segment_start + n * d;
}

static bool is_projected_point_within_segment(SimVec3 p_point, SimVec3 p_segment_start, SimVec3 p_segment_end) {
	SimVec3 p = p_point - p_segment_start;
	SimVec3 n = p_segment_end - p_segment_start;
	float l2 = n.length_squared();
	if (l2 < 1e-20f) {
		return false;
	}

	float d = n.dot(p);

	if (d < 0.0f) {
		return false;
	}
	else if (d > l2) {
		return false;
	}
	else {
		return true;
	}
}

inline static float remap_float(float value, float istart, float istop, float ostart, float ostop) {
	return ostart + (ostop - ostart) * ((value - istart) / (istop - istart));
}

inline static float move_float_toward(float value, float target, float step) {
	return fmaxf(fminf(value + step, target), value - step);
}

inline static float lerp(float a, float b, float t)
{
	return a + t * (b - a);
}

inline static float randf_range(float min, float max) {
	static uint32_t state = 0x12345678u;
	state = state * 1664525u + 1013904223u;
	const float t = static_cast<float>(state & 0x00ffffffu) * (1.0f / 16777215.0f);
	return min + (max - min) * t;
}

inline static const SimVec3 project_to_plane(const SimVec3 &p_norm, const float &p_dist, const SimVec3 &in_point)
{
	float dist = (p_norm.dot(in_point) - p_dist);
	return in_point - p_norm * dist;
}

inline static void ray_scale(const float scale, const SimVec3 &start, const SimVec3 &end, SimVec3 &out)
{
	out = start + scale * (end - start);
}

inline static const bool swept_sphere_vs_swept_sphere(float radiusA,
                              float radiusB,
                              const SimVec3 &p0A, const SimVec3 &p1A,
                              const SimVec3 &p0B, const SimVec3 &p1B,
                              float &outTOI,
                              uint32_t &startedIntersecting)
{
    constexpr float kEpsilon = 1.1920929e-7f;
    outTOI = 100.0f;
    startedIntersecting = 0;

    SimVec3 r0 = p0A - p0B;
    SimVec3 r1 = p1A - p1B;
    SimVec3 v = r1 - r0;

    const float a = v.dot(v);
    const float b = r0.dot(v);
    const float radiusSum = radiusA + radiusB;
    const float c = r0.dot(r0) - radiusSum * radiusSum;

    if (c <= 0.0f) {
        startedIntersecting = 1;
        outTOI = 0.0f;
        return false;
    }

    if (a < kEpsilon) {
        return false;
    }

    const float discriminant = b * b - a * c;
    if (discriminant < 0.0f) {
        return false;
    }

    const float sqrt_disc = sqrtf(discriminant);
    const float t = (-b - sqrt_disc) / a;
    if (t < 0.0f || t > 1.0f) {
        return false;
    }

    outTOI = t;
    return true;
}
