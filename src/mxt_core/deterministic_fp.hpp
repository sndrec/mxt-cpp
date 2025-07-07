#pragma once

#include <cmath>
#include <cstdint>	// uint32_t

namespace deterministic_fp {

	constexpr float PI = 3.14159265358979323846f;
	constexpr float TWO_PI = 6.28318530717958647692f;
	constexpr float INV_TWO_PI = 0.15915494309189533577f;	// 1 / (2π)
	constexpr float PI_OVER_2 = 1.57079632679489661923f;
	constexpr float PI_OVER_4 = 0.78539816339744830962f;

	inline float absf(float x) {
		union {
			float		f;
			uint32_t	i;
		} u{ x };
		u.i &= 0x7FFFFFFFu;
		return u.f;
	}

	inline float wrap_minus_pi_to_pi(float x) {
		// round-to-nearest, ties away-from-zero (same rule your cast+0.5 used)
		float k = std::roundf(x * INV_TWO_PI);
		return x - k * TWO_PI;
	}

	inline float poly_sin(float r) {
		float r2 = r * r;
		return r * (1.0f
			+ r2 * (-1.666665710e-1f
			+ r2 * (8.332996250e-3f
			+ r2 * (-1.951529589e-4f
			+ r2 * 2.592075552e-6f))));
	}

	inline float poly_cos(float r) {
		float r2 = r * r;
		return 1.0f
			+ r2 * (-0.5f
			+ r2 * (4.166664568e-2f
			+ r2 * (-1.388731625e-3f
			+ r2 * 2.443315711e-5f)));
	}

	inline float sinf(float x) {
		return poly_sin(x);
	}

	inline float cosf(float x) {
		return poly_cos(x);
	}

	inline float atan2f(float y, float x) {
		if (x == 0.0f) {
			if (y > 0.0f)	return PI_OVER_2;
			if (y < 0.0f)	return -PI_OVER_2;
			return 0.0f;	// (0,0) – pick 0
		}

		float abs_y = absf(y);
		float angle;

		if (x > 0.0f) {
			float r = (x - abs_y) / (x + abs_y);
			angle = PI_OVER_4 - PI_OVER_4 * r;
		}
		else {
			float r = (x + abs_y) / (abs_y - x);
			angle = 3.0f * PI_OVER_4 - PI_OVER_4 * r;
		}

		return y < 0.0f ? -angle : angle;
	}

}
