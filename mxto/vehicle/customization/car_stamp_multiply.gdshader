shader_type spatial;

render_mode depth_draw_never, ambient_light_disabled, cull_back;
stencil_mode write, compare_always, 1;

uniform sampler2D stamp_atlas : source_color, filter_linear_mipmap_anisotropic, repeat_disable;
uniform sampler2D in_lightwarp : filter_nearest_mipmap_anisotropic, repeat_disable;
uniform sampler2D in_specwarp : filter_nearest_mipmap_anisotropic, repeat_disable;
uniform sampler2D in_normal : filter_linear_mipmap_anisotropic, repeat_disable;
uniform sampler2D in_albedo : filter_linear_mipmap_anisotropic, repeat_disable;

void vertex() {
	TANGENT = vec3(1.0, 0.0, 0.0);
	BINORMAL = vec3(0.0, 0.0, -1.0);
	NORMAL = vec3(0.0, 1.0, 0.0);
}

float fresnel(float amount, vec3 normal, vec3 view) {
	return pow((1.0 - clamp(dot(normalize(normal), normalize(view)), 0.0, 1.0)), amount);
}

void fragment() {
	vec3 normal_1 = texture(in_normal, UV).rgb * 2.0 - 1.0;
	NORMAL = normalize(TANGENT * normal_1.x + BINORMAL * normal_1.y + NORMAL * normal_1.z);
	vec3 base_colour = texture(in_albedo, UV).rgb;
	vec4 stamp = texture(stamp_atlas, UV2) * COLOR;
	float stamp_alpha = clamp(stamp.a, 0.0, 1.0);
	if (stamp_alpha <= 0.01) {
		discard;
	}
	vec3 stamp_multiply = mix(vec3(1.0), clamp(stamp.rgb, vec3(0.0), vec3(1.0)), stamp_alpha);
	ALBEDO = base_colour * stamp_multiply;
}

void light() {
	float light_val = (dot(NORMAL, LIGHT) * ATTENUATION) * 0.5 + 0.5;
	vec3 H = normalize(LIGHT * 2.0 + VIEW);
	float ndoth = max(dot(NORMAL, H), 0.0);
	float spec = pow(ndoth, 8.0) * ATTENUATION;
	spec = (spec + light_val) * 0.5;
	float fres = 1.0 - fresnel(1.0, NORMAL, VIEW);
	float fres_outline = fresnel(16.0, NORMAL, VIEW);
	light_val = clamp(light_val - fres * 0.1, 0.0, 1.0);
	light_val = clamp(light_val + fres_outline * 2.0, 0.0, 1.0);
	vec3 final_light_val = texture(in_lightwarp, vec2(light_val, 0.0)).rgb;
	DIFFUSE_LIGHT += final_light_val * LIGHT_COLOR * 0.333;
	SPECULAR_LIGHT += texture(in_specwarp, vec2(spec, 0.0)).rgb;
}
