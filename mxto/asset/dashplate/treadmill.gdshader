shader_type spatial;

uniform sampler2D treadmill_tex : source_color;
global uniform float fog_dist;

#include "res://asset/shader/ordered_dither.gdshaderinc"

void fragment() {
	ALBEDO = texture(treadmill_tex, UV - vec2(0.0, TIME * 2.0)).rgb * 2.0;
	float coverage = track_distance_fade_coverage(VERTEX, fog_dist);
	float threshold = ordered_dither_threshold(FRAGCOORD, UV);
	threshold = mix(0.5, threshold, dither_strength);
	if (coverage < threshold) {
		discard;
	}
}

