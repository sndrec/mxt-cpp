shader_type spatial;

render_mode ambient_light_disabled;//, skip_vertex_transform;
stencil_mode write, compare_always, 1;

uniform sampler2D in_lightwarp : filter_nearest_mipmap_anisotropic, repeat_disable;
uniform sampler2D in_specwarp : filter_nearest_mipmap_anisotropic, repeat_disable;
uniform sampler2D in_normal : filter_linear_mipmap_anisotropic, repeat_disable;
uniform sampler2D in_albedo : filter_linear_mipmap_anisotropic, repeat_disable;
uniform sampler2D in_paint_mask : filter_linear_mipmap_anisotropic, repeat_disable;
uniform vec3 in_overlay_colour : source_color;
uniform float livery_colour_strength = 0.0;
uniform vec3 livery_primary_colour : source_color = vec3(1.0, 1.0, 1.0);
uniform vec3 livery_secondary_colour : source_color = vec3(1.0, 1.0, 1.0);
uniform vec3 livery_accent_colour : source_color = vec3(1.0, 1.0, 1.0);

void vertex() {
	TANGENT = vec3(1.0, 0.0, 0.0);
	BINORMAL = vec3(0.0, 0.0, -1.0);
	NORMAL = vec3(0.0, 1.0, 0.0);
}

float fresnel(float amount, vec3 normal, vec3 view) {
	return pow((1.0 - clamp(dot(normalize(normal), normalize(view)), 0.0, 1.0 )), amount);
}

void fragment() {
	vec3 normal_1 = texture(in_normal, UV).rgb * 2.0 - 1.0;
	//vec3 normal_2 = texture(in_normal, UV + vec2(-0.002, 0.001)).rgb * 2.0 - 1.0;
	//vec3 normal_3 = texture(in_normal, UV + vec2(0.002, -0.001)).rgb * 2.0 - 1.0;
	//vec3 normal_4 = texture(in_normal, UV + vec2(-0.001, -0.002)).rgb * 2.0 - 1.0;
	//vec3 normal_combined = normal_1 + normal_2 + normal_3 + normal_4;
	NORMAL = normalize(TANGENT * normal_1.x + BINORMAL * normal_1.y + NORMAL * normal_1.z);
	vec3 base_colour = texture(in_albedo, UV).rgb;
	vec3 paint_mask = clamp(texture(in_paint_mask, UV).rgb, vec3(0.0), vec3(1.0));
	float mask_sum = paint_mask.r + paint_mask.g + paint_mask.b;
	vec3 mask_weights = paint_mask / max(mask_sum, 0.0001);
	float paint_coverage = clamp(mask_sum, 0.0, 1.0);
	float base_value = dot(base_colour, vec3(0.299, 0.587, 0.114));
	vec3 paint_colour =
		livery_primary_colour * mask_weights.r +
		livery_secondary_colour * mask_weights.g +
		livery_accent_colour * mask_weights.b;
	vec3 painted_colour = base_value * paint_colour * 1.25;
	vec3 final_colour = mix(base_colour, painted_colour, clamp(livery_colour_strength, 0.0, 1.0) * paint_coverage);
	ALBEDO = final_colour;// * fres;
	//EMISSION = in_overlay_colour + COLOR.rgb;
}

void light() {
	float light_val = (dot(NORMAL, LIGHT) * ATTENUATION) * 0.5 + 0.5;
	vec3  H		= normalize(LIGHT * 2.0 + VIEW);		// half-vector
	float ndoth	= max(dot(NORMAL, H), 0.0);
	float spec	= pow(ndoth, 8.0) * ATTENUATION;
	spec = (spec + light_val) * 0.5;
	float fres = 1.0 - fresnel(1.0, NORMAL, VIEW);
	float fres_outline = fresnel(16.0, NORMAL, VIEW);
	light_val = clamp(light_val - fres * 0.1, 0.0, 1.0);
	light_val = clamp(light_val + fres_outline * 2.0, 0.0, 1.0);
	//light_val = clamp(light_val + spec * 0.1, 0.0, 1.0);
	vec3 final_light_val = texture(in_lightwarp, vec2(light_val, 0.0f)).rgb;
	DIFFUSE_LIGHT += final_light_val * LIGHT_COLOR * 0.333;// * 0.3;
	SPECULAR_LIGHT += texture(in_specwarp, vec2(spec, 0.0f)).rgb;
}
