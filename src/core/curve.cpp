#include "core/curve.h"
#include "core/math_utils.h"
#include <immintrin.h>
#include <algorithm>

float Curve::sample(float in_t) const
{
	float value = 0.0f;
	sample_with_derivative(in_t, &value, nullptr);
	return value;
};

float Curve::sample_derivative(float in_t) const
{
	float derivative = 0.0f;
	sample_with_derivative(in_t, nullptr, &derivative);
	return derivative;
};

void Curve::sample_with_derivative(float in_t, float *value_out, float *derivative_out) const
{
	if (value_out)
	{
		*value_out = 0.0f;
	}
	if (derivative_out)
	{
		*derivative_out = 0.0f;
	}
	if (num_keyframes == 0)
	{
		return;
	};
	if (num_keyframes == 1)
	{
		if (value_out)
		{
			*value_out = keyframes[0].value;
		}
		return;
	};
	if (in_t < keyframes[0].time)
	{
		if (value_out)
		{
			*value_out = keyframes[0].value;
		}
		return;
	};
	if (in_t == keyframes[0].time)
	{
		if (value_out)
		{
			*value_out = keyframes[0].value;
		}
		if (derivative_out)
		{
			*derivative_out = keyframes[0].tangent_out;
		}
		return;
	};
	if (in_t > keyframes[num_keyframes - 1].time)
	{
		if (value_out)
		{
			*value_out = keyframes[num_keyframes - 1].value;
		}
		return;
	};
	if (in_t == keyframes[num_keyframes - 1].time)
	{
		if (value_out)
		{
			*value_out = keyframes[num_keyframes - 1].value;
		}
		if (derivative_out)
		{
			*derivative_out = keyframes[num_keyframes - 1].tangent_in;
		}
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
	float p1 = keyframes[start_key_index].value;
	float p2 = keyframes[start_key_index + 1].value;
	float dist = keyframes[start_key_index + 1].time - keyframes[start_key_index].time;
	if (dist == 0)
	{
		if (value_out)
		{
			*value_out = p2;
		}
		return;
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
	if (value_out)
	{
		*value_out = dp_dv;
	}
	if (derivative_out)
	{
		const float deriv_u =
			3.0f * ((p1_handle - p1) * omt2 +
				2.0f * (p2_handle - p1_handle) * omt * in_t +
				(p2 - p2_handle) * t2);
		*derivative_out = deriv_u / (dist * 3.0f);
	}
};

void RoadTransformCurve::precompute() {
	if (num_keyframes < 2) {
		return;
	}

	for (int k = 0; k < num_keyframes - 1; ++k) {
		float t0 = times[k];
		float t1 = times[k + 1];
		float dt = (t1 > t0) ? (t1 - t0) : 1.0f;
		inv_dt[k] = 1.0f / dt;
		float dist = dt * (1.0f / 3.0f);

#ifdef __AVX2__
		__m256 distv = _mm256_set1_ps(dist);
		for (int c = 0; c < 16; c += 8) {
			int base = k * 16 + c;
			__m256 p0 = _mm256_load_ps(values + base);
			__m256 p1 = _mm256_load_ps(values + 16 + base);
			__m256 to = _mm256_load_ps(tangent_out + base);
			__m256 ti = _mm256_load_ps(tangent_in + 16 + base);
#ifdef __FMA__
			__m256 h0 = _mm256_fmadd_ps(distv, to, p0);
			__m256 h1 = _mm256_fnmadd_ps(distv, ti, p1);
#else
			__m256 h0 = _mm256_add_ps(p0, _mm256_mul_ps(distv, to));
			__m256 h1 = _mm256_sub_ps(p1, _mm256_mul_ps(distv, ti));
#endif
			__m256 a = _mm256_add_ps(
				_mm256_sub_ps(p1, p0),
				_mm256_mul_ps(_mm256_set1_ps(3.0f), _mm256_sub_ps(h0, h1)));
			__m256 b = _mm256_mul_ps(
				_mm256_set1_ps(3.0f),
				_mm256_add_ps(_mm256_sub_ps(p0, _mm256_mul_ps(_mm256_set1_ps(2.0f), h0)), h1));
			__m256 c0 = _mm256_mul_ps(_mm256_set1_ps(3.0f), _mm256_sub_ps(h0, p0));
			__m256 d = p0;

			_mm256_store_ps(coef_a + base, a);
			_mm256_store_ps(coef_b + base, b);
			_mm256_store_ps(coef_c + base, c0);
			_mm256_store_ps(coef_d + base, d);
		}
#else
		__m128 distv = _mm_set1_ps(dist);
		for (int c = 0; c < 16; c += 4) {
			int base = k * 16 + c;
			__m128 p0 = _mm_load_ps(values + base);
			__m128 p1 = _mm_load_ps(values + 16 + base);
			__m128 to = _mm_load_ps(tangent_out + base);
			__m128 ti = _mm_load_ps(tangent_in + 16 + base);
#ifdef __FMA__
			__m128 h0 = _mm_fmadd_ps(distv, to, p0);
			__m128 h1 = _mm_fnmadd_ps(distv, ti, p1);
#else
			__m128 h0 = _mm_add_ps(p0, _mm_mul_ps(distv, to));
			__m128 h1 = _mm_sub_ps(p1, _mm_mul_ps(distv, ti));
#endif
			__m128 a = _mm_add_ps(
				_mm_sub_ps(p1, p0),
				_mm_mul_ps(_mm_set1_ps(3.0f), _mm_sub_ps(h0, h1)));
			__m128 b = _mm_mul_ps(
				_mm_set1_ps(3.0f),
				_mm_add_ps(_mm_sub_ps(p0, _mm_mul_ps(_mm_set1_ps(2.0f), h0)), h1));
			__m128 c0 = _mm_mul_ps(_mm_set1_ps(3.0f), _mm_sub_ps(h0, p0));
			__m128 d = p0;

			_mm_store_ps(coef_a + base, a);
			_mm_store_ps(coef_b + base, b);
			_mm_store_ps(coef_c + base, c0);
			_mm_store_ps(coef_d + base, d);
		}
#endif
	}
}

static inline void _set_road_transform_from_values(RoadTransform &out, const float *values)
{
	out.t3d.basis = SimBasis(
		values[3], values[4], values[5],
		values[6], values[7], values[8],
		values[9], values[10], values[11]);
	out.t3d.origin = SimVec3(values[0], values[1], values[2]);
	out.scale = SimVec3(values[12], values[13], values[14]);
}

static inline void _set_road_transform_zero(RoadTransform &out)
{
	out.t3d.basis = SimBasis(
		0.0f, 0.0f, 0.0f,
		0.0f, 0.0f, 0.0f,
		0.0f, 0.0f, 0.0f);
	out.t3d.origin = SimVec3();
	out.scale = SimVec3();
}

static inline int _road_curve_segment_for_t(const RoadTransformCurve &curve, float &in_t)
{
	if (in_t <= curve.times[0]) {
		in_t = curve.times[0];
		return 0;
	}
	if (in_t >= curve.times[curve.num_keyframes - 1]) {
		in_t = curve.times[curve.num_keyframes - 1];
		return curve.num_keyframes - 2;
	}

	int lo = 0;
	int hi = curve.num_keyframes - 1;
	while (lo + 1 < hi) {
		const int mid = (lo + hi) >> 1;
		if (in_t >= curve.times[mid]) {
			lo = mid;
		} else {
			hi = mid;
		}
	}
	return lo;
}

void RoadTransformCurve::sample(RoadTransform &out, float in_t) {
	RoadTransform derivative;
	sample_with_derivative(out, derivative, in_t);
}

void RoadTransformCurve::sample_with_derivative(
	RoadTransform &out,
	RoadTransform &derivative_out,
	float in_t) {
	if (num_keyframes == 0) {
		_set_road_transform_zero(derivative_out);
		return;
	}
	if (num_keyframes == 1) {
		_set_road_transform_from_values(out, values);
		_set_road_transform_zero(derivative_out);
		return;
	}

	const int k = _road_curve_segment_for_t(*this, in_t);

	float u = (in_t - times[k]) * inv_dt[k];
	float u2 = u * u;
	float u3 = u2 * u;

#ifdef __AVX2__
	alignas(32) float sampled[16];
	alignas(32) float sampled_derivative[16];
	__m256 uv  = _mm256_set1_ps(u);
	__m256 u2v = _mm256_set1_ps(u2);
	__m256 u3v = _mm256_set1_ps(u3);
	__m256 deriv_scale = _mm256_set1_ps(inv_dt[k]);
	for (int chunk = 0; chunk < 2; ++chunk) {
		int base = k * 16 + chunk * 8;
		__m256 a = _mm256_load_ps(coef_a + base);
		__m256 b = _mm256_load_ps(coef_b + base);
		__m256 c = _mm256_load_ps(coef_c + base);
		__m256 d = _mm256_load_ps(coef_d + base);
#if defined(__FMA__)
		__m256 r = _mm256_fmadd_ps(a, u3v,
			_mm256_fmadd_ps(b, u2v,
				_mm256_fmadd_ps(c, uv, d)));
#else
		__m256 r = _mm256_add_ps(_mm256_mul_ps(a, u3v),
			_mm256_add_ps(_mm256_mul_ps(b, u2v),
				_mm256_add_ps(_mm256_mul_ps(c, uv), d)));
#endif
		_mm256_store_ps(sampled + chunk * 8, r);

#if defined(__FMA__)
		__m256 deriv = _mm256_fmadd_ps(
			_mm256_set1_ps(3.0f), _mm256_mul_ps(a, u2v),
			_mm256_fmadd_ps(_mm256_set1_ps(2.0f), _mm256_mul_ps(b, uv), c));
#else
		__m256 deriv = _mm256_add_ps(
			_mm256_mul_ps(_mm256_set1_ps(3.0f), _mm256_mul_ps(a, u2v)),
			_mm256_add_ps(_mm256_mul_ps(_mm256_set1_ps(2.0f), _mm256_mul_ps(b, uv)), c));
#endif
		deriv = _mm256_mul_ps(deriv, deriv_scale);
		_mm256_store_ps(sampled_derivative + chunk * 8, deriv);
	}
#else
	alignas(16) float sampled[16];
	alignas(16) float sampled_derivative[16];
	__m128 uv  = _mm_set1_ps(u);
	__m128 u2v = _mm_set1_ps(u2);
	__m128 u3v = _mm_set1_ps(u3);
	__m128 deriv_scale = _mm_set1_ps(inv_dt[k]);
	for (int chunk = 0; chunk < 4; ++chunk) {
		int base = k * 16 + chunk * 4;
		__m128 a = _mm_load_ps(coef_a + base);
		__m128 b = _mm_load_ps(coef_b + base);
		__m128 c = _mm_load_ps(coef_c + base);
		__m128 d = _mm_load_ps(coef_d + base);
#if defined(__FMA__)
		__m128 r = _mm_fmadd_ps(a, u3v,
			_mm_fmadd_ps(b, u2v,
				_mm_fmadd_ps(c, uv, d)));
#else
		__m128 r = _mm_add_ps(_mm_mul_ps(a, u3v),
			_mm_add_ps(_mm_mul_ps(b, u2v),
				_mm_add_ps(_mm_mul_ps(c, uv), d)));
#endif
		_mm_store_ps(sampled + chunk * 4, r);

#if defined(__FMA__)
		__m128 deriv = _mm_fmadd_ps(
			_mm_set1_ps(3.0f), _mm_mul_ps(a, u2v),
			_mm_fmadd_ps(_mm_set1_ps(2.0f), _mm_mul_ps(b, uv), c));
#else
		__m128 deriv = _mm_add_ps(
			_mm_mul_ps(_mm_set1_ps(3.0f), _mm_mul_ps(a, u2v)),
			_mm_add_ps(_mm_mul_ps(_mm_set1_ps(2.0f), _mm_mul_ps(b, uv)), c));
#endif
		deriv = _mm_mul_ps(deriv, deriv_scale);
		_mm_store_ps(sampled_derivative + chunk * 4, deriv);
	}
#endif

	_set_road_transform_from_values(out, sampled);
	_set_road_transform_from_values(derivative_out, sampled_derivative);
}

void RoadTransformCurve::sample4(RoadTransform out[4], const float in_t[4])
{
	RoadTransform derivative[4];
	sample4_with_derivative(out, derivative, in_t);
}

void RoadTransformCurve::sample4_with_derivative(
	RoadTransform out[4],
	RoadTransform derivative_out[4],
	const float in_t[4])
{
	if (num_keyframes == 0) {
		for (int lane = 0; lane < 4; ++lane) {
			_set_road_transform_zero(out[lane]);
			_set_road_transform_zero(derivative_out[lane]);
		}
		return;
	}
	if (num_keyframes == 1) {
		for (int lane = 0; lane < 4; ++lane) {
			_set_road_transform_from_values(out[lane], values);
			_set_road_transform_zero(derivative_out[lane]);
		}
		return;
	}

	float t[4] = { in_t[0], in_t[1], in_t[2], in_t[3] };
	int k[4];
	for (int lane = 0; lane < 4; ++lane) {
		k[lane] = _road_curve_segment_for_t(*this, t[lane]);
	}

	const bool same_segment = k[0] == k[1] && k[0] == k[2] && k[0] == k[3];
	const __m128 u = _mm_set_ps(
		(t[3] - times[k[3]]) * inv_dt[k[3]],
		(t[2] - times[k[2]]) * inv_dt[k[2]],
		(t[1] - times[k[1]]) * inv_dt[k[1]],
		(t[0] - times[k[0]]) * inv_dt[k[0]]);
	const __m128 u2 = _mm_mul_ps(u, u);
	const __m128 u3 = _mm_mul_ps(u2, u);
	const __m128 deriv_scale = _mm_set_ps(inv_dt[k[3]], inv_dt[k[2]], inv_dt[k[1]], inv_dt[k[0]]);

	alignas(16) float sampled[4][16];
	alignas(16) float sampled_derivative[4][16];
	if (same_segment) {
		const int same_k = k[0];
		const __m128 deriv_scale_same = _mm_set1_ps(inv_dt[same_k]);
		for (int c = 0; c < 16; ++c) {
			const int base = same_k * 16 + c;
			const __m128 a = _mm_set1_ps(coef_a[base]);
			const __m128 b = _mm_set1_ps(coef_b[base]);
			const __m128 cc = _mm_set1_ps(coef_c[base]);
			const __m128 d = _mm_set1_ps(coef_d[base]);

#if defined(__FMA__)
			const __m128 r = _mm_fmadd_ps(a, u3, _mm_fmadd_ps(b, u2, _mm_fmadd_ps(cc, u, d)));
			__m128 deriv = _mm_fmadd_ps(
				_mm_set1_ps(3.0f), _mm_mul_ps(a, u2),
				_mm_fmadd_ps(_mm_set1_ps(2.0f), _mm_mul_ps(b, u), cc));
#else
			const __m128 r = _mm_add_ps(_mm_mul_ps(a, u3),
				_mm_add_ps(_mm_mul_ps(b, u2), _mm_add_ps(_mm_mul_ps(cc, u), d)));
			__m128 deriv = _mm_add_ps(
				_mm_mul_ps(_mm_set1_ps(3.0f), _mm_mul_ps(a, u2)),
				_mm_add_ps(_mm_mul_ps(_mm_set1_ps(2.0f), _mm_mul_ps(b, u)), cc));
#endif
			deriv = _mm_mul_ps(deriv, deriv_scale_same);
			alignas(16) float lanes[4];
			alignas(16) float deriv_lanes[4];
			_mm_store_ps(lanes, r);
			_mm_store_ps(deriv_lanes, deriv);
			for (int lane = 0; lane < 4; ++lane) {
				sampled[lane][c] = lanes[lane];
				sampled_derivative[lane][c] = deriv_lanes[lane];
			}
		}

		for (int lane = 0; lane < 4; ++lane) {
			_set_road_transform_from_values(out[lane], sampled[lane]);
			_set_road_transform_from_values(derivative_out[lane], sampled_derivative[lane]);
		}
		return;
	}

	for (int c = 0; c < 16; ++c) {
		const __m128 a = _mm_set_ps(
			coef_a[k[3] * 16 + c],
			coef_a[k[2] * 16 + c],
			coef_a[k[1] * 16 + c],
			coef_a[k[0] * 16 + c]);
		const __m128 b = _mm_set_ps(
			coef_b[k[3] * 16 + c],
			coef_b[k[2] * 16 + c],
			coef_b[k[1] * 16 + c],
			coef_b[k[0] * 16 + c]);
		const __m128 cc = _mm_set_ps(
			coef_c[k[3] * 16 + c],
			coef_c[k[2] * 16 + c],
			coef_c[k[1] * 16 + c],
			coef_c[k[0] * 16 + c]);
		const __m128 d = _mm_set_ps(
			coef_d[k[3] * 16 + c],
			coef_d[k[2] * 16 + c],
			coef_d[k[1] * 16 + c],
			coef_d[k[0] * 16 + c]);

#if defined(__FMA__)
		const __m128 r = _mm_fmadd_ps(a, u3, _mm_fmadd_ps(b, u2, _mm_fmadd_ps(cc, u, d)));
		__m128 deriv = _mm_fmadd_ps(
			_mm_set1_ps(3.0f), _mm_mul_ps(a, u2),
			_mm_fmadd_ps(_mm_set1_ps(2.0f), _mm_mul_ps(b, u), cc));
#else
		const __m128 r = _mm_add_ps(_mm_mul_ps(a, u3),
			_mm_add_ps(_mm_mul_ps(b, u2), _mm_add_ps(_mm_mul_ps(cc, u), d)));
		__m128 deriv = _mm_add_ps(
			_mm_mul_ps(_mm_set1_ps(3.0f), _mm_mul_ps(a, u2)),
			_mm_add_ps(_mm_mul_ps(_mm_set1_ps(2.0f), _mm_mul_ps(b, u)), cc));
#endif
		deriv = _mm_mul_ps(deriv, deriv_scale);
		alignas(16) float lanes[4];
		alignas(16) float deriv_lanes[4];
		_mm_store_ps(lanes, r);
		_mm_store_ps(deriv_lanes, deriv);
		for (int lane = 0; lane < 4; ++lane) {
			sampled[lane][c] = lanes[lane];
			sampled_derivative[lane][c] = deriv_lanes[lane];
		}
	}

	for (int lane = 0; lane < 4; ++lane) {
		_set_road_transform_from_values(out[lane], sampled[lane]);
		_set_road_transform_from_values(derivative_out[lane], sampled_derivative[lane]);
	}
}
