shader_type spatial;

render_mode unshaded, blend_add, cull_disabled, depth_draw_never, fog_disabled, skip_vertex_transform;

uniform sampler2D plasma_texture : filter_linear_mipmap, repeat_disable;
uniform sampler2D depth_texture : hint_depth_texture, repeat_disable, filter_nearest;
uniform float depth_fade_distance : hint_range(0.01, 2.0, 0.01) = 0.35;
global uniform float fog_dist;

#include "res://asset/shader/distance_fade.gdshaderinc"

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
	// accumulation inside Godot's linear framebuffer. Bright texels use the
	// four-particle reference that produces the characteristic red mid-band;
	// faint fringe texels approach a single contribution so sparse tails do not
	// inherit the brightness compensation intended for dense overlap.
	vec3 texture_code = texture(plasma_texture, UV).rgb;
	vec3 tint_code = floor(COLOR.rgb * 255.0 + 0.5) / 255.0;
	vec3 tev_code = floor(texture_code * tint_code * 255.0 + 0.5) / 255.0;
	float texture_strength = max(texture_code.r, max(texture_code.g, texture_code.b));
	float overlap_weight = smoothstep(0.08, 0.35, texture_strength);
	float reference_overlap = mix(1.0, 4.0, overlap_weight);
	vec3 gx_accumulated = min(tev_code * reference_overlap, vec3(1.0));

	float raw_scene_depth = textureLod(depth_texture, SCREEN_UV, 0.0).r;
	vec3 scene_ndc = vec3(SCREEN_UV * 2.0 - 1.0, raw_scene_depth);
	vec4 scene_view = INV_PROJECTION_MATRIX * vec4(scene_ndc, 1.0);
	float scene_depth = -scene_view.z / scene_view.w;
	float plasma_depth = -VERTEX.z;
	float depth_fade = smoothstep(
		0.0,
		depth_fade_distance,
		scene_depth - plasma_depth);
	float coverage = depth_fade * COLOR.a * track_distance_fade_coverage(VERTEX, fog_dist);
	ALBEDO = srgb_to_linear(gx_accumulated) / reference_overlap;
	ALPHA = coverage;
}
