shader_type spatial;
render_mode blend_add, depth_draw_always, cull_disabled, unshaded;

uniform vec4 color_1 : source_color;
uniform vec4 color_2 : source_color;
uniform sampler2D texture_albedo : source_color, filter_linear_mipmap, repeat_enable;

uniform float distance_fade_min : hint_range(0.0, 4096.0, 0.01);
uniform float distance_fade_max : hint_range(0.0, 4096.0, 0.01);

uniform vec2 finishline_size;

varying vec2 old_uv;

void vertex()
{
	old_uv = UV;
	UV *= finishline_size * 0.5;
}

void fragment()
{
	vec4 albedo_tex = texture(texture_albedo, UV);
	ALBEDO = mix(color_1, color_2, albedo_tex.r).rgb *
		(sin(TIME * 60.0 * PI) * 0.2 + 0.9);

	float dist_from_edge = min(old_uv.x, 1.0 - old_uv.x) * 4.0;
	dist_from_edge = min(1.0, dist_from_edge * 0.5);
	ALBEDO *= (1.0 - pow(dist_from_edge * 0.5, 0.5)) + 0.25;
	ALBEDO *= pow(min(1.0, dist_from_edge * 2.0), 2.0);
	ALBEDO = max(ALBEDO * 4.0, vec3(0.0));

	float fade_distance = length(VERTEX);
	float fade = clamp(
		smoothstep(distance_fade_min, distance_fade_max, fade_distance),
		0.0,
		1.0);
	ALBEDO *= fade;
}
