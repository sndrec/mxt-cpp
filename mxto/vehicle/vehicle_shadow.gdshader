shader_type spatial;

render_mode unshaded, depth_draw_never, cull_disabled;
stencil_mode read, compare_not_equal, 1;

global uniform float fog_dist;

#include "res://asset/shader/ordered_dither.gdshaderinc"

void vertex() {
	// Called for every vertex the material is visible on.
}

void fragment() {
	float coverage = track_distance_fade_coverage(VERTEX, fog_dist) * COLOR.a;
	float threshold = ordered_dither_threshold(FRAGCOORD, UV);
	threshold = mix(0.5, threshold, dither_strength);
	if (coverage < threshold) {
		discard;
	}
	ALBEDO = vec3(0.0);
	DEPTH = max(0.0, FRAGCOORD.z + 0.002);
}

//void light() {
//	// Called for every pixel for every light affecting the material.
//	// Uncomment to replace the default light processing function with this one.
//}
