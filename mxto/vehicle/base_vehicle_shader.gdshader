shader_type spatial;

render_mode ambient_light_disabled;//, skip_vertex_transform;
stencil_mode write, compare_always, 1;

uniform sampler2D in_lightwarp : filter_nearest_mipmap_anisotropic, repeat_disable;
uniform sampler2D in_specwarp : filter_nearest_mipmap_anisotropic, repeat_disable;
uniform sampler2D in_normal : filter_linear_mipmap_anisotropic, repeat_disable;
uniform sampler2D in_albedo : filter_linear_mipmap_anisotropic, repeat_disable;
uniform sampler2D in_paint_mask : filter_linear_mipmap_anisotropic, repeat_disable;
uniform bool use_mesh_normals = false;
uniform sampler2D cross_hatch : source_color, filter_linear_mipmap_anisotropic, repeat_enable;
uniform vec3 in_overlay_colour : source_color;
uniform float livery_colour_strength = 0.0;
uniform vec3 livery_primary_colour : source_color = vec3(1.0, 1.0, 1.0);
uniform vec3 livery_secondary_colour : source_color = vec3(1.0, 1.0, 1.0);
uniform vec3 livery_accent_colour : source_color = vec3(1.0, 1.0, 1.0);
uniform int diagnostic_mode = 0;

varying vec2 use_screen_pixel;
varying vec3 hatch_normal;

void vertex() {
	hatch_normal = normalize(MODELVIEW_NORMAL_MATRIX * NORMAL);
	if (!use_mesh_normals) {
		TANGENT = vec3(1.0, 0.0, 0.0);
		BINORMAL = vec3(0.0, 0.0, -1.0);
		NORMAL = vec3(0.0, 1.0, 0.0);
	}
}

float fresnel(float amount, vec3 normal, vec3 view) {
	return pow((1.0 - clamp(dot(normalize(normal), normalize(view)), 0.0, 1.0 )), amount);
}

float sample_lit_hatch(vec2 screen_pixel, vec2 anchor_pixel, vec2 gradient_dir, vec2 gradient_tangent, vec2 hatch_texture_size) {
	vec2 local_pixel = screen_pixel - anchor_pixel;
	vec2 hatch_pixel = vec2(dot(local_pixel, gradient_dir), dot(local_pixel, gradient_tangent));
	return texture(cross_hatch, hatch_pixel / hatch_texture_size).r;
}

vec2 light_gradient_from_normal(vec3 normal, vec3 light_direction) {
	float normal_z_sign = mix(-1.0, 1.0, step(0.0, normal.z));
	float normal_z = normal_z_sign * max(abs(normal.z), 0.1);
	vec2 view_light_gradient = light_direction.xy - normal.xy * (light_direction.z / normal_z);
	return vec2(-view_light_gradient.x, view_light_gradient.y);
}

float lighting_aligned_hatch(vec2 screen_pixel, vec3 normal, vec3 light_direction) {
	vec2 hatch_texture_size = vec2(textureSize(cross_hatch, 0));
	vec2 gradient = light_gradient_from_normal(normal, light_direction);
	float gradient_len = length(gradient);
	vec2 gradient_dir = gradient / max(gradient_len, 0.000001);
	vec2 gradient_tangent = vec2(-gradient_dir.y, gradient_dir.x);
	float lattice_size = 96.0;
	vec2 lattice_pos = screen_pixel / lattice_size;
	vec2 lattice_cell = floor(lattice_pos);
	vec2 lattice_blend = smoothstep(vec2(0.0), vec2(1.0), fract(lattice_pos));
	vec2 anchor_00 = lattice_cell * lattice_size;
	vec2 anchor_10 = anchor_00 + vec2(lattice_size, 0.0);
	vec2 anchor_01 = anchor_00 + vec2(0.0, lattice_size);
	vec2 anchor_11 = anchor_00 + vec2(lattice_size);
	float hatch_00 = sample_lit_hatch(screen_pixel, anchor_00, gradient_dir, gradient_tangent, hatch_texture_size);
	float hatch_10 = sample_lit_hatch(screen_pixel, anchor_10, gradient_dir, gradient_tangent, hatch_texture_size);
	float hatch_01 = sample_lit_hatch(screen_pixel, anchor_01, gradient_dir, gradient_tangent, hatch_texture_size);
	float hatch_11 = sample_lit_hatch(screen_pixel, anchor_11, gradient_dir, gradient_tangent, hatch_texture_size);
	float rotated_hatch = mix(mix(hatch_00, hatch_10, lattice_blend.x), mix(hatch_01, hatch_11, lattice_blend.x), lattice_blend.y);
	float screen_hatch = texture(cross_hatch, screen_pixel / hatch_texture_size).r;
	float rotate_weight = smoothstep(0.001, 0.01, gradient_len);
	return mix(screen_hatch, rotated_hatch, rotate_weight);
}

void fragment() {
	use_screen_pixel = SCREEN_UV * VIEWPORT_SIZE;
	vec3 normal_1 = texture(in_normal, UV).rgb * 2.0 - 1.0;
	//vec3 normal_2 = texture(in_normal, UV + vec2(-0.002, 0.001)).rgb * 2.0 - 1.0;
	//vec3 normal_3 = texture(in_normal, UV + vec2(0.002, -0.001)).rgb * 2.0 - 1.0;
	//vec3 normal_4 = texture(in_normal, UV + vec2(-0.001, -0.002)).rgb * 2.0 - 1.0;
	//vec3 normal_combined = normal_1 + normal_2 + normal_3 + normal_4;
	vec3 base_normal = NORMAL;
	NORMAL = normalize(TANGENT * normal_1.x + BINORMAL * normal_1.y + base_normal * normal_1.z);
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
	if (diagnostic_mode > 0) {
		if (diagnostic_mode == 1) final_colour = base_colour;
		else if (diagnostic_mode == 2) final_colour = normal_1 * 0.5 + 0.5;
		else if (diagnostic_mode == 3) final_colour = paint_mask;
		else final_colour = vec3(1.0, 0.1, 0.75);
		ALBEDO = vec3(0.0);
		EMISSION = final_colour;
	} else {
		ALBEDO = final_colour;// * fres;
		EMISSION = in_overlay_colour + COLOR.rgb;
	}
}

void light() {
	float raw_light_val = (dot(NORMAL, LIGHT) * ATTENUATION) * 0.5 + 0.5;
	float light_val = raw_light_val;
	vec3  H		= normalize(LIGHT * 2.0 + VIEW);		// half-vector
	float ndoth	= max(dot(NORMAL, H), 0.0);
	float spec	= pow(ndoth, 8.0) * ATTENUATION;
	spec = (spec + light_val) * 0.5;
	float fres = 1.0 - fresnel(1.0, NORMAL, VIEW);
	float fres_outline = fresnel(16.0, NORMAL, VIEW);
	light_val = clamp(light_val - fres * 0.1, 0.0, 1.0);
	light_val = clamp(light_val + fres_outline * 2.0, 0.0, 1.0);
	//light_val = clamp(light_val + spec * 0.1, 0.0, 1.0);
	float cross_hatch_float = (lighting_aligned_hatch(use_screen_pixel, normalize(hatch_normal), LIGHT) - 0.5) * 0.05;
	vec3 final_light_val = texture(in_lightwarp, vec2(clamp(light_val + cross_hatch_float, 0.0, 1.0), 0.0f)).rgb;
	DIFFUSE_LIGHT += final_light_val * LIGHT_COLOR * 0.333;// * 0.3;
	SPECULAR_LIGHT += texture(in_specwarp, vec2(clamp(spec + cross_hatch_float, 0.0, 1.0), 0.0f)).rgb;
}
