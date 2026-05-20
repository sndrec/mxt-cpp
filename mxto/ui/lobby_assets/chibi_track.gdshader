shader_type spatial;
render_mode blend_mix, depth_draw_opaque, cull_back, diffuse_burley, specular_schlick_ggx;

uniform vec4 albedo : source_color = vec4(0.541176, 0.541176, 0.541176, 1.0);
uniform sampler2D texture_albedo : source_color, repeat_disable;
uniform vec3 uv1_scale = vec3(10.0, 3.0, 1.0);
uniform vec3 uv1_offset = vec3(0.0, 0.0, 0.0);

void vertex() {
	UV = UV * uv1_scale.xy + uv1_offset.xy;
}

void fragment() {
	vec2 base_uv = UV;
	if (int(floor(base_uv.x)) % 2 == 1) {
		base_uv.x = 1.0 - fract(base_uv.x);
	}
	if (int(floor(base_uv.y)) % 2 == 1) {
		base_uv.y = 1.0 - fract(base_uv.y);
	}
	base_uv = fract(base_uv);
	vec4 albedo_tex = texture(texture_albedo, base_uv);
	ALBEDO = albedo.rgb * albedo_tex.rgb;
	ALPHA = 1.0;
}
