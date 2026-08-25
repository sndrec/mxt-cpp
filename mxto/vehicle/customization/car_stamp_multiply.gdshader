shader_type spatial;

render_mode depth_draw_never, ambient_light_disabled, cull_back;
stencil_mode write, compare_always, 1;

uniform sampler2D base_stamp_atlas : source_color, filter_linear_mipmap_anisotropic, repeat_disable;
uniform sampler2D custom_stamp_atlas : source_color, filter_linear_mipmap_anisotropic, repeat_disable;
uniform sampler2D stamp_visibility_mask : filter_nearest, repeat_disable;
uniform sampler2D in_lightwarp : filter_nearest_mipmap_anisotropic, repeat_disable;
uniform sampler2D in_lightwarp_2 : filter_nearest_mipmap_anisotropic, repeat_disable;
uniform sampler2D in_lightwarp_2_fresnel : filter_nearest_mipmap_anisotropic, repeat_disable;
uniform sampler2D in_specwarp : filter_nearest_mipmap_anisotropic, repeat_disable;
uniform sampler2D in_specwarp_fresnel : filter_nearest_mipmap_anisotropic, repeat_disable;
uniform sampler2D in_normal : filter_linear_mipmap_anisotropic, repeat_disable;
uniform sampler2D in_albedo : filter_linear_mipmap_anisotropic, repeat_disable;
global uniform float fog_dist;

#include "res://asset/shader/ordered_dither.gdshaderinc"

varying vec4 stamp_mask_data;
varying float stamp_source_flag;

void vertex() {
	stamp_mask_data = CUSTOM0;
	stamp_source_flag = CUSTOM1.x;
	TANGENT = vec3(1.0, 0.0, 0.0);
	BINORMAL = vec3(0.0, 0.0, -1.0);
	NORMAL = vec3(0.0, 1.0, 0.0);
}

float fresnel(float amount, vec3 normal, vec3 view) {
	return pow((1.0 - clamp(dot(normalize(normal), normalize(view)), 0.0, 1.0)), amount);
}

void fragment() {
	vec3 normal_1 = texture(in_normal, UV).rgb * 2.0 - 1.0;
	vec3 base_normal = NORMAL;
	NORMAL = normalize(TANGENT * normal_1.x + BINORMAL * normal_1.y + base_normal * normal_1.z);
	vec3 base_colour = texture(in_albedo, UV).rgb;
	vec4 base_stamp = texture(base_stamp_atlas, UV2);
	vec4 custom_stamp = texture(custom_stamp_atlas, UV2);
	vec4 stamp = mix(base_stamp, custom_stamp, step(0.5, stamp_source_flag)) * COLOR;
	vec2 visibility_mask = texture(stamp_visibility_mask, stamp_mask_data.xy).rg;
	float visibility = mix(1.0, step(visibility_mask.r - stamp_mask_data.w, stamp_mask_data.z), step(0.5, visibility_mask.g));
	float coverage = clamp(stamp.a * visibility, 0.0, 1.0) * track_distance_fade_coverage(VERTEX, fog_dist);
	float threshold = ordered_dither_threshold(FRAGCOORD, UV);
	threshold = mix(0.5, threshold, dither_strength);
	if (coverage < threshold) {
		discard;
	}
	vec3 stamp_multiply = clamp(stamp.rgb, vec3(0.0), vec3(1.0));
	ALBEDO = base_colour * stamp_multiply;
}

void light() {
	float raw_light_val = (dot(NORMAL, LIGHT) * ATTENUATION) * 0.5 + 0.5;
	float light_val = raw_light_val;
	vec3 H = normalize(LIGHT * 2.0 + VIEW);
	float ndoth = max(dot(NORMAL, H), 0.0);
	float spec = pow(ndoth, 8.0) * ATTENUATION;
	spec = (spec + light_val) * 0.5;
	float fres = 1.0 - fresnel(1.0, NORMAL, VIEW);
	float fres_outline = fresnel(2.0, NORMAL, VIEW);
	vec3 final_light_val = texture(in_lightwarp, vec2(clamp(light_val, 0.0, 1.0), 0.0)).rgb;
	vec3 final_light_val_2 = texture(in_lightwarp_2, vec2(clamp(light_val + spec * 0.1, 0.0, 1.0), 0.0)).rgb;
	final_light_val_2 *= texture(in_lightwarp_2_fresnel, vec2(fres_outline, 0.0)).rgb;
	DIFFUSE_LIGHT += (final_light_val + final_light_val_2) * LIGHT_COLOR * 0.333;
	vec3 spec_add = texture(in_specwarp, vec2(clamp(spec, 0.0, 1.0), 0.0)).rgb * final_light_val * LIGHT_COLOR * 0.333;
	spec_add *= texture(in_specwarp_fresnel, vec2(fres_outline, 0.0)).rgb;
	SPECULAR_LIGHT += spec_add;
}
