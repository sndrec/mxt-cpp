shader_type spatial;

render_mode unshaded, blend_add, cull_disabled, depth_draw_never, fog_disabled, skip_vertex_transform;

uniform sampler2D plasma_texture : filter_linear_mipmap, repeat_disable;

vec3 srgb_to_linear(vec3 color) {
	vec3 low = color / 12.92;
	vec3 high = pow((color + 0.055) / 1.055, vec3(2.4));
	return mix(low, high, step(vec3(0.04045), color));
}

void vertex() {
	// The four transform columns carry the final camera-space GX quad vertices
	// in UV winding order. They are data, not an instance/world transform.
	vec3 vertex_0 = MODEL_MATRIX[0].xyz;
	vec3 vertex_1 = MODEL_MATRIX[1].xyz;
	vec3 vertex_2 = MODEL_MATRIX[2].xyz;
	vec3 vertex_3 = MODEL_MATRIX[3].xyz;
	vec3 bottom = mix(vertex_0, vertex_1, UV.x);
	vec3 top = mix(vertex_3, vertex_2, UV.x);
	VERTEX = mix(bottom, top, UV.y);
}

void fragment() {
	// EFF_RENSFREA01 is modulated by the particle RGB and submitted with GX's
	// ONE/ONE additive blend into an 8-bit EFB. Approximate that code-space
	// accumulation inside Godot's linear framebuffer around four overlapping
	// particles, where the characteristic red mid-band is most visible.
	const float reference_overlap = 4.0;
	vec3 texture_code = texture(plasma_texture, UV).rgb;
	vec3 tint_code = floor(COLOR.rgb * 255.0 + 0.5) / 255.0;
	vec3 tev_code = floor(texture_code * tint_code * 255.0 + 0.5) / 255.0;
	vec3 gx_accumulated = min(tev_code * reference_overlap, vec3(1.0));
	ALBEDO = srgb_to_linear(gx_accumulated) / reference_overlap;
	ALPHA = 1.0;
}
