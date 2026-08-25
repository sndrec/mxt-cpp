shader_type spatial;
render_mode cull_disabled, unshaded;

uniform sampler2D projection_tex : source_color;
uniform sampler2D projection_displacement_tex : source_color;
uniform sampler2D booster_color_ramp : source_color, repeat_disable;
uniform float booster_intensity;
uniform float boost_time;
global uniform float fog_dist;

#include "res://asset/shader/ordered_dither.gdshaderinc"

void vertex() {
	vec3 displacement = texture(projection_displacement_tex, UV + vec2(boost_time * 0.005, boost_time * 0.01)).rgb;
	displacement = (displacement - 0.5) * 0.04 * (0.5 + booster_intensity * 0.5);
	VERTEX += displacement * (booster_intensity * 0.2 + 1.0);
}

void fragment() {
	float coverage = pow(max(0.0, 0.75 - texture(projection_tex, UV).r), 8.0) * (11.0 + booster_intensity);
	coverage = pow(clamp(coverage - distance(UV, vec2(0.5, 0.5)), 0.0, 1.0), 16.0 / booster_intensity);
	coverage *= min(1.0, pow(length(VERTEX) * 0.1, 3.0)) * 0.5;
	coverage *= track_distance_fade_coverage(VERTEX, fog_dist);
	float threshold = ordered_dither_threshold(FRAGCOORD, UV);
	threshold = mix(0.5, threshold, dither_strength);
	if (coverage < threshold) {
		discard;
	}
	vec3 final_color = texture(booster_color_ramp, vec2(booster_intensity * 0.2, 0.0)).rgb * 2.0;
	ALBEDO = final_color;
}

void light() {
	// Called for every pixel for every light affecting the material.
}
