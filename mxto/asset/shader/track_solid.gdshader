shader_type spatial;
render_mode depth_draw_opaque, cull_disabled, fog_disabled;

uniform vec4 albedo : source_color = vec4(1.0);
uniform vec4 emission_color : source_color = vec4(0.0, 0.0, 0.0, 1.0);
uniform float emission_energy = 1.0;
global uniform float fog_dist;

#include "res://asset/shader/ordered_dither.gdshaderinc"

void fragment() {
	ALBEDO = albedo.rgb * COLOR.rgb;
	EMISSION = emission_color.rgb * emission_energy * COLOR.rgb;
	METALLIC = 0.0;
	SPECULAR = 0.0;
	ROUGHNESS = 1.0;

	float coverage = track_distance_fade_coverage(VERTEX, fog_dist);
	float threshold = ordered_dither_threshold(FRAGCOORD, UV);
	threshold = mix(0.5, threshold, dither_strength);
	if (coverage < threshold) {
		discard;
	}
}
