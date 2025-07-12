#pragma once

#include "godot_cpp/classes/engine.hpp"
#include <godot_cpp/core/memory.hpp>
#include <godot_cpp/classes/random_number_generator.hpp>
#include "mxt_core/deterministic_fp.hpp"
#include "godot_cpp/core/math.hpp"
#include "godot_cpp/variant/utility_functions.hpp"

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
const godot::Basis BASIS_IDENTITY = godot::Basis(1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f);
const godot::Transform3D T3D_IDENTITY = godot::Transform3D(BASIS_IDENTITY, godot::Vector3(0.0f, 0.0f, 0.0f));

template <typename T> static int sgn(T val) {
	return (T(0) < val) - (val < T(0));
}

// TODO: create a line segment struct that precomputes length + normal
// these funcs are only used for the checkpoint math, which is all done
// on cached line segments that do not change
// so we can precompute lengths and normals to avoid costly math

static godot::Vector3 get_closest_point_to_segment(godot::Vector3 p_point, godot::Vector3 p_segment_start, godot::Vector3 p_segment_end) {
	godot::Vector3 p = p_point - p_segment_start;
	godot::Vector3 n = p_segment_end - p_segment_start;
	float l2 = n.length_squared();
	if (l2 < 1e-20f) {
		return p_segment_start;
	}

	float d = n.dot(p) / l2;

	if (d <= 0.0f) {
		return p_segment_start;
	}
	else if (d >= 1.0f) {
		return p_segment_end;
	}
	else {
		return p_segment_start + n * d;
	}
}

static godot::Vector3 get_closest_point_to_segment_uncapped(godot::Vector3 p_point, godot::Vector3 p_segment_start, godot::Vector3 p_segment_end) {
	godot::Vector3 p = p_point - p_segment_start;
	godot::Vector3 n = p_segment_end - p_segment_start;
	float l2 = n.length_squared();
	if (l2 < 1e-20f) {
		return p_segment_start;
	}

	float d = n.dot(p) / l2;

	return p_segment_start + n * d;
}

static float get_closest_t_on_segment(godot::Vector3 p_point, godot::Vector3 p_segment_start, godot::Vector3 p_segment_end) {
	godot::Vector3 p = p_point - p_segment_start;
	godot::Vector3 n = p_segment_end - p_segment_start;
	float l2 = n.length_squared();
	if (l2 < 1e-20f) {
		return 0.0f;
	}

	float d = n.dot(p) / l2;

	return d;
}

static bool is_projected_point_within_segment(godot::Vector3 p_point, godot::Vector3 p_segment_start, godot::Vector3 p_segment_end) {
	godot::Vector3 p = p_point - p_segment_start;
	godot::Vector3 n = p_segment_end - p_segment_start;
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
	// allocate via Godot’s memnew
	godot::RandomNumberGenerator *rng = memnew(godot::RandomNumberGenerator);
	// seed once from high-res clock
	rng->randomize();
	// get your random float
	float res = rng->randf_range(min, max);
	// free it
	memdelete(rng);
	return res;
}

inline static const float dist_to_plane(godot::Vector3 &p_norm, float &p_dist, godot::Vector3 &in_point)
{
	return (p_norm.dot(in_point) - p_dist);
}

inline static const godot::Vector3 project_to_plane(const godot::Vector3 &p_norm, const float &p_dist, const godot::Vector3 &in_point)
{
	float dist = (p_norm.dot(in_point) - p_dist);
	return in_point - p_norm * dist;
}

inline static void ray_scale(const float scale, const godot::Vector3 &start, const godot::Vector3 &end, godot::Vector3 &out)
{
	out = start + scale * (end - start);
}

inline static const bool swept_sphere_vs_swept_sphere(float radiusA,
                              float radiusB,
                              const godot::Vector3 &p0A, const godot::Vector3 &p1A,
                              const godot::Vector3 &p0B, const godot::Vector3 &p1B,
                              float &outTOI,
                              uint32_t &startedIntersecting)
{
    constexpr float kEpsilon = 1.1920929e-7f;   // ~= FLT_EPSILON

    /* ------------------------------------------------------------------ */
    outTOI              = 100.0f;  // “infinite” time – never collides
    startedIntersecting = 0;
    /* ------------------------------------------------------------------ */

    /* Relative motion set-up                                             */
    godot::Vector3 r0 = p0A - p0B;

    godot::Vector3 r1 = p1A - p1B;

    godot::Vector3 v = r1 - r0;

    const float a = v.dot(v);        // |v|²
    const float b = r0.dot(v);        // r0·v
    const float radiusSum = radiusA + radiusB;
    const float c = r0.dot(r0) - radiusSum * radiusSum;

    /* ------------------------------------------------------------------ */
    /* Already intersecting?                                              */
    if (c <= 0.0f)
    {
        startedIntersecting = 1;
        outTOI = 0.0f;
        return false;                  // no *new* collision
    }

    /* ------------------------------------------------------------------ */
    /* No relative motion → can’t collide if not already intersecting     */
    if (a < kEpsilon)
    {
        return false;
    }

    /* ------------------------------------------------------------------ */
    /* Solve quadratic for time-of-impact                                  */
    const float discriminant = b * b - a * c;
    if (discriminant < -kEpsilon)
    {
        return false;                  // no real roots – paths miss
    }

    const float sqrtDisc = sqrtf(discriminant);
    const float t = (-b - sqrtDisc) / a;   // earliest contact time

    if (t < 0.0f || t > 1.0f)
    {
        return false;                  // contact occurs outside sweep
    }

    outTOI = t;
    return true;
}