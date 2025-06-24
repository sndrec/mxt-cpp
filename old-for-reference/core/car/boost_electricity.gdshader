shader_type spatial;

render_mode blend_add, cull_disabled, unshaded;

void fragment() {
	float x_ratio = min(UV.x, 1.0 - UV.x) * 60.0;
	ALBEDO = vec3(0.4, 0.9, 1.0) * x_ratio * (0.25 - UV2.x);
}
