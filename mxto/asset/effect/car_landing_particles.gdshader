shader_type spatial;

render_mode unshaded, blend_add, depth_draw_never;

uniform sampler2D landing_tex : source_color;
global uniform float fog_dist;

#include "res://asset/shader/distance_fade.gdshaderinc"

void fragment() {
	vec4 landing = texture(landing_tex, vec2(UV.x, -UV.y));
	float coverage = landing.a * COLOR.a * track_distance_fade_coverage(VERTEX, fog_dist);
	ALBEDO = landing.rgb * 0.35 * COLOR.rgb;
	ALPHA = coverage;
}

//void light() {
	// Called for every pixel for every light affecting the material.
	// Uncomment to replace the default light processing function with this one.
//}
