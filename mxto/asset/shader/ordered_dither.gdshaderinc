// Higher values = finer ordered dither under SSAA.
// 1.0 = normal strength.
// Lowering slightly can make fades look a bit softer.
uniform float dither_strength : hint_range(0.1, 2.0) = 1.0;

// Optional: set to true if you want the dither pattern locked to the screen.
// Set false if you want it to move with UVs/object space instead.
uniform bool screen_space_dither = true;

// 8x8 Bayer matrix, normalized to 0..63 order.
// We'll convert to thresholds in 0..1 at use time.
const int BAYER8[64] = int[](
	 0, 48, 12, 60,  3, 51, 15, 63,
	32, 16, 44, 28, 35, 19, 47, 31,
	 8, 56,  4, 52, 11, 59,  7, 55,
	40, 24, 36, 20, 43, 27, 39, 23,
	 2, 50, 14, 62,  1, 49, 13, 61,
	34, 18, 46, 30, 33, 17, 45, 29,
	10, 58,  6, 54,  9, 57,  5, 53,
	42, 26, 38, 22, 41, 25, 37, 21
);

float bayer8_threshold(ivec2 p) {
	int x = int(mod(float(p.x), 8.0));
	int y = int(mod(float(p.y), 8.0));
	int index = x + y * 8;

	// +0.5 centers the threshold buckets.
	return (float(BAYER8[index]) + 0.5) / 64.0;
}

float ordered_dither_threshold(vec4 in_frag, vec2 in_uv) {
	ivec2 p;

	if (screen_space_dither) {
		p = ivec2(int(in_frag.x), int(in_frag.y));
	} else {
		// Object/UV anchored alternative.
		// Scale factor can be adjusted depending on your texture density.
		vec2 uv_pixels = in_uv * 256.0;
		p = ivec2(int(floor(uv_pixels.x)), int(floor(uv_pixels.y)));
	}

	return bayer8_threshold(p);
}
