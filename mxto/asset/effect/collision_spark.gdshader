shader_type spatial;

render_mode unshaded, blend_add, cull_disabled, depth_draw_never, fog_disabled, skip_vertex_transform;

uniform sampler2D spark_texture : source_color, filter_linear, repeat_disable;
global uniform float fog_dist;

#include "res://asset/shader/ordered_dither.gdshaderinc"

varying flat float brightness_compensation;

void vertex() {
	// The native particle batch encodes the retained camera-space tail in the
	// first transform column and the interpolated camera-space head in origin.
	vec3 tail = MODEL_MATRIX[0].xyz;
	vec3 head = MODEL_MATRIX[3].xyz;
	float sprite_size = max(INSTANCE_CUSTOM.x, 0.00001);
	float head_depth = max(abs(head.z), 0.00001);
	float tail_depth = max(abs(tail.z), 0.00001);
	vec2 head_screen = head.xy / head_depth;
	vec2 tail_screen = tail.xy / tail_depth;
	vec2 particle_dir = head_screen - tail_screen;
	float dir_length = length(particle_dir);
	if (dir_length < 0.00001) {
		particle_dir = vec2(1.0, 0.0);
		dir_length = 1.0;
	}
	particle_dir /= dir_length;
	vec2 particle_right = vec2(particle_dir.y, -particle_dir.x);
	float true_distance = length(head_screen - tail_screen) * abs(max(head.z, tail.z));
	float clamped_distance = min(true_distance, sprite_size);
	vec2 size_fix = -particle_dir * (sprite_size - clamped_distance);
	vec2 position_fix = particle_dir * (sprite_size - clamped_distance) * 0.5;

	vec3 view_position = VERTEX.x < 0.0 ? tail : head;
	view_position.xy += position_fix;
	if (VERTEX.x < 0.0) {
		view_position.xy += size_fix;
	}
	view_position.xy += particle_right * VERTEX.y * sprite_size * 3.0;
	brightness_compensation = 1.0 / max(min(true_distance, 1.0) * 10.0, 0.001);
	VERTEX = view_position;
}

void fragment() {
	vec4 spark = texture(spark_texture, UV);
	float coverage = spark.a * COLOR.a * track_distance_fade_coverage(VERTEX, fog_dist);
	float threshold = ordered_dither_threshold(FRAGCOORD, UV);
	threshold = mix(0.5, threshold, dither_strength);
	if (coverage < threshold) {
		discard;
	}
	vec3 color = spark.rgb * COLOR.rgb * brightness_compensation;
	ALBEDO = color;
	EMISSION = color;
}
