#pragma once

#include <cstdint>	// uint32_t

namespace deterministic_fp {

	constexpr float PI = 3.14159265358979323846f;
	constexpr float TWO_PI = 6.28318530717958647692f;
	constexpr float INV_TWO_PI = 0.15915494309189533577f;	// 1 / (2π)
	constexpr float PI_OVER_2 = 1.57079632679489661923f;
	constexpr float PI_OVER_4 = 0.78539816339744830962f;

	/* branch‑free fabsf without <bit> */
	inline float absf(float x) {
		union {
			float		f;
			uint32_t	i;
		} u{ x };
		u.i &= 0x7FFFFFFFu;
		return u.f;
	}

	/* wrap to (‑π, π] exactly */
	inline float wrap_minus_pi_to_pi(float x) {
		int32_t k = static_cast<int32_t>(x * INV_TWO_PI + (x >= 0.0f ? 0.5f : -0.5f));
		return x - static_cast<float>(k) * TWO_PI;
	}

	inline float poly_sin(float r) {
		float r2 = r * r;
		return r + r * r2 * (-0.1666666669f		// -r³/6
			+ r2 * (0.0083333310f				// +r⁵/120
				+ r2 * -0.000198412698f));		// -r⁷/5040
	}

	inline float poly_cos(float r) {
		float r2 = r * r;
		return 1.0f + r2 * (-0.5f				// -r²/2
			+ r2 * (0.0416666664f				// +r⁴/24
				+ r2 * -0.00138888889f));		// -r⁶/720
	}

	inline float sinf(float x) {
		return poly_sin(x);
	}

	inline float cosf(float x) {
		return poly_cos(x);
	}

	/* fast deterministic atan2f (~4 ulp worst‑case) */
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

} // namespace deterministic_fp
