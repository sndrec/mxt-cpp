shader_type spatial;

render_mode ambient_light_disabled, cull_disabled;

instance uniform vec3 base_color : source_color;
instance uniform vec3 secondary_color : source_color;
instance uniform vec3 tertiary_color : source_color;

instance uniform vec3 overlay_color : source_color;
instance uniform float depth_offset = -5000;

uniform sampler2D albedo_texture : source_color;
uniform sampler2D color_overlay_texture : source_color, repeat_disable;


uniform sampler2D specular_ramp : source_color, repeat_disable;
uniform sampler2D fresnel_ramp : source_color, repeat_disable;
uniform sampler2D diffuse_ramp : source_color, repeat_disable;
global uniform sampler2D global_cross_hatch;


void vertex() {
	// Called for every vertex the material is visible on.
}

vec3 rgb2hsv(vec3 c)
{
    vec4 K = vec4(0.0, -1.0 / 3.0, 2.0 / 3.0, -1.0);
    vec4 p = mix(vec4(c.bg, K.wz), vec4(c.gb, K.xy), step(c.b, c.g));
    vec4 q = mix(vec4(p.xyw, c.r), vec4(c.r, p.yzx), step(p.x, c.r));

    float d = q.x - min(q.w, q.y);
    float e = 1.0e-10;
    return vec3(abs(q.z + (q.w - q.y) / (6.0 * d + e)), d / (q.x + e), q.x);
}

vec3 hsv2rgb(vec3 c)
{
    vec4 K = vec4(1.0, 2.0 / 3.0, 1.0 / 3.0, 3.0);
    vec3 p = abs(fract(c.xxx + K.xyz) * 6.0 - K.www);
    return c.z * mix(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);
}

vec4 linearize_depth(float in_depth, vec2 in_screen_uv, mat4 inv_projection_matrix)
{
	vec3 ndc = vec3(in_screen_uv * 2.0 - 1.0, in_depth);
	vec4 view = inv_projection_matrix * vec4(ndc, 1.0);
	view.xyz /= view.w;
	view.z = -view.z;
	return view;
}

float unlinearize_depth(vec4 in_view, mat4 projection_matrix)
{
	in_view.z = -in_view.z;
	in_view.xyz *= in_view.w;
	vec3 ndc = (projection_matrix * in_view).xyz;
	return ndc.z;
}

varying vec2 use_screen_uv;

void fragment() {
	use_screen_uv = SCREEN_UV;
	vec4 albedo_texture_sampled = texture(albedo_texture, UV);
	vec4 colormap_texture_sampled = texture(color_overlay_texture, UV);
	vec3 albedo_hsv = rgb2hsv(albedo_texture_sampled.rgb);
	vec3 albedo_desaturated = hsv2rgb(vec3(albedo_hsv.x, 0.0, albedo_hsv.z));
	albedo_texture_sampled = mix(albedo_texture_sampled, vec4(albedo_desaturated * base_color, albedo_texture_sampled.a), colormap_texture_sampled.r);
	albedo_texture_sampled = mix(albedo_texture_sampled, vec4(albedo_desaturated * secondary_color, albedo_texture_sampled.a), colormap_texture_sampled.g);
	albedo_texture_sampled = mix(albedo_texture_sampled, vec4(albedo_desaturated * tertiary_color, albedo_texture_sampled.a), colormap_texture_sampled.b);
	EMISSION += overlay_color;
	ALBEDO = albedo_texture_sampled.rgb + overlay_color;
	METALLIC = 0.0;
	SPECULAR = 0.0;
	ROUGHNESS = 1.0;

	//ALPHA = 1.0;

	//vec4 linear_view = linearize_depth(FRAGCOORD.z, SCREEN_UV, INV_PROJECTION_MATRIX);
	//linear_view.z += depth_offset;
	//DEPTH = unlinearize_depth(linear_view, PROJECTION_MATRIX);
}

float fresnel(float amount, vec3 normal, vec3 view)
{
	return pow((1.0 - clamp(dot(normalize(normal), normalize(view)), 0.0, 1.0 )), amount);
}

void light() {
	float cross_hatch_float = (texture(global_cross_hatch, use_screen_uv).r - 0.5) * 0.05;
	vec3 light_mix_factor = texture(diffuse_ramp, vec2(clamp(dot(NORMAL, LIGHT) + cross_hatch_float, 0.0, 1.0) * ATTENUATION, 0.0)).rgb;
	DIFFUSE_LIGHT += light_mix_factor * LIGHT_COLOR * ALBEDO;
	SPECULAR_LIGHT += texture(fresnel_ramp, vec2(fresnel(1.0, NORMAL, VIEW) + cross_hatch_float, 0.0)).rgb * 100.0 * ALBEDO;
	vec3 reflected = reflect(LIGHT, NORMAL);
	vec3 use_view = -VIEW;
	float dotprod = dot(use_view , reflected);
	float spec_add = pow(max(0.0, dotprod), 2.0);
	vec3 spec_add_color = texture(specular_ramp, vec2(spec_add + cross_hatch_float, 0.0)).rgb;
	float light_mix = clamp(dot(NORMAL, LIGHT), 0.0, 1.0) * ATTENUATION;
	SPECULAR_LIGHT += spec_add_color * light_mix * 100.0 * ALBEDO;
}