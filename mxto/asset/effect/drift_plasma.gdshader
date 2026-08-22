shader_type spatial;

render_mode unshaded, blend_add, cull_disabled, depth_draw_never, fog_disabled, skip_vertex_transform;

uniform sampler2D plasma_texture : source_color, filter_linear_mipmap, repeat_disable;

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
	// ONE/ONE additive blend, depth testing enabled, and depth writes disabled.
	vec3 color = texture(plasma_texture, UV).rgb * COLOR.rgb;
	ALBEDO = color;
	ALPHA = 1.0;
}
