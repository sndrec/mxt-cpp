shader_type spatial;

render_mode unshaded, blend_add, depth_test_disabled, cull_disabled;

uniform sampler2D in_texture : filter_linear_mipmap_anisotropic, repeat_disable;
uniform bool selected = false;
uniform bool clicked = false;

void fragment() {
	vec4 sampled = texture(in_texture, vec2(1.0, 1.0) - UV.yx);
	ALBEDO = sampled.rgb;
	float use_alpha = selected ? sampled.a * 0.2 : sampled.a * 0.05;
	use_alpha = clicked ? sampled.a * 1.0 : use_alpha;
	ALPHA = use_alpha;
}
