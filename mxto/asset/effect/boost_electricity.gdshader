shader_type spatial;

render_mode cull_disabled, unshaded, depth_draw_never;

global uniform float fog_dist;

#include "res://asset/shader/ordered_dither.gdshaderinc"

void fragment() {
	float coverage = track_distance_fade_coverage(VERTEX, fog_dist);
	float threshold = ordered_dither_threshold(FRAGCOORD, UV);
	threshold = mix(0.5, threshold, dither_strength);
	if (coverage < threshold) {
		discard;
	}
	float x_ratio = min(UV.x, 1.0 - UV.x) * 1.5;
	x_ratio = round(x_ratio);
	x_ratio = x_ratio * 0.85 + 0.15;
	ALBEDO = vec3(x_ratio) * vec3(0.4, 0.9, 1.0) * 2.0;
}
