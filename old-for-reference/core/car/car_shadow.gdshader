shader_type spatial;

render_mode unshaded, depth_draw_always, skip_vertex_transform;

uniform sampler2D car_shadow : source_color;
uniform sampler2D car_shadow_gradient : source_color, repeat_disable;

uniform float shadow_blur : hint_range(0.0, 0.05, 0.001);

global uniform sampler2D global_cross_hatch;

void vertex() {
	vec3 SCALED_VERTEX = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz;
	vec3 dir = CAMERA_POSITION_WORLD - SCALED_VERTEX;
	SCALED_VERTEX += dir * 0.1;
	VERTEX = (VIEW_MATRIX * vec4(SCALED_VERTEX, 1.0)).xyz;
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

void fragment() {
	float camera_colour_avg = 0.0;
	int x_samples = 4;
	int y_samples = 4;
	for(int i = 0; i < x_samples * y_samples; i++){
		int x = i % x_samples;
		int y = i / y_samples;
		float fx = float(x) / float(x_samples) - 0.5;
		float fy = float(y) / float(y_samples) - 0.5;
		camera_colour_avg += texture(car_shadow, UV + vec2(shadow_blur * fx, shadow_blur * fy)).a;
	}
	camera_colour_avg /= float(x_samples * y_samples);
	//vec4 camera_colour_avg = (camera_colour_1 + camera_colour_2 + camera_colour_3 + camera_colour_4) * 0.25;
	float cross_hatch = texture(global_cross_hatch, SCREEN_UV).r - 0.5;
	vec4 final_car_shadow = texture(car_shadow_gradient, vec2(camera_colour_avg + cross_hatch, 0.0));
	ALBEDO = final_car_shadow.rgb;
	ALPHA = final_car_shadow.a;

	//float offset = -5000.0;
	//vec4 linear_view = linearize_depth(FRAGCOORD.z, SCREEN_UV, INV_PROJECTION_MATRIX);
	//linear_view.z += offset;
	//DEPTH = unlinearize_depth(linear_view, PROJECTION_MATRIX);
}
