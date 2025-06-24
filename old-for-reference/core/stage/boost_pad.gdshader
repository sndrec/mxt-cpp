shader_type spatial;
render_mode cull_disabled, unshaded;

uniform sampler2D projection_tex : source_color;
uniform sampler2D projection_displacement_tex : source_color;
uniform sampler2D booster_color_ramp : source_color, repeat_disable;
uniform float booster_intensity;
uniform float boost_time;

void vertex() {
	vec3 displacement = texture(projection_displacement_tex, UV + vec2(boost_time * 0.005 * booster_intensity, boost_time * 0.01 * booster_intensity)).rgb;
	displacement = (displacement - 0.5) * 0.04 * (0.5 + booster_intensity * 0.5);
	VERTEX += displacement;
}

void fragment() {
	ALPHA = pow(max(0.0, 0.75 - texture(projection_tex, UV).r), 8.0) * (11.0 + booster_intensity);
	ALPHA = pow(clamp(ALPHA - distance(UV, vec2(0.5, 0.5)), 0.0, 1.0), 16.0 / booster_intensity);
	ALPHA = ALPHA * min(1.0, pow(length(VERTEX) * 0.1, 3.0)) * 0.5;
	vec3 final_color = texture(booster_color_ramp, vec2(booster_intensity * 0.2, 0.0)).rgb * 2.0;
	ALBEDO = final_color;
}

void light() {
	// Called for every pixel for every light affecting the material.
}
