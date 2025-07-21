shader_type spatial;

render_mode blend_mix, cull_disabled, unshaded;

void fragment() {
	float x_ratio = min(UV.x, 1.0 - UV.x) * 1.5;
	x_ratio = round(x_ratio);
	x_ratio = x_ratio * 0.85 + 0.15;
	ALBEDO = vec3(x_ratio) * vec3(0.4, 0.9, 1.0) * 2.0;
}
