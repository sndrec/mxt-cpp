#pragma once

#include "godot_cpp/classes/engine.hpp"
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
	auto now = std::chrono::high_resolution_clock::now();
	auto seed = std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count();
	godot::UtilityFunctions::seed(static_cast<int64_t>(seed));
	return godot::UtilityFunctions::randf_range(min, max);
}