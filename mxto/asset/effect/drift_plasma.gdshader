shader_type spatial;

render_mode unshaded, blend_add, cull_disabled, depth_draw_never, fog_disabled, skip_vertex_transform;

uniform sampler2D plasma_texture : source_color, filter_linear, repeat_disable;

void vertex() {
	// Native instances encode GX-style camera-space ribbon endpoints instead
	// of ordinary world transforms: tail in column zero, head in origin.
	vec3 tail = MODEL_MATRIX[0].xyz;
	vec3 head = MODEL_MATRIX[3].xyz;
	float width = max(INSTANCE_CUSTOM.x, 0.00001);
	float head_depth = max(abs(head.z), 0.00001);
	float tail_depth = max(abs(tail.z), 0.00001);
	vec2 head_screen = head.xy / head_depth;
	vec2 tail_screen = tail.xy / tail_depth;
	vec2 ribbon_direction = head_screen - tail_screen;
	if (length(ribbon_direction) < 0.00001) {
		ribbon_direction = vec2(1.0, 0.0);
	} else {
		ribbon_direction = normalize(ribbon_direction);
	}
	vec2 ribbon_right = vec2(ribbon_direction.y, -ribbon_direction.x);
	vec3 view_position = VERTEX.x < 0.0 ? tail : head;
	view_position.xy += ribbon_right * VERTEX.y * width;
	view_position.xy += ribbon_direction * width * (VERTEX.x < 0.0 ? -0.35 : 0.35);
	VERTEX = view_position;
}

void fragment() {
	// EFF_SMOKE_D is an intensity texture. GX draws it with ONE/ONE additive
	// blending and supplies the reddish particle color through the vertex data.
	float intensity = texture(plasma_texture, UV).r;
	vec3 color = vec3(intensity) * COLOR.rgb;
	ALBEDO = color;
	EMISSION = color;
	ALPHA = 1.0;
}
