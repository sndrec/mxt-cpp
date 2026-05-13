// NOTE: Shader automatically converted from Godot Engine 4.2.rc1's StandardMaterial3D.

shader_type spatial;
render_mode blend_mix,cull_back,depth_draw_always,shadows_disabled;
uniform vec4 albedo : source_color;
uniform sampler2D texture_albedo : source_color,filter_linear_mipmap,repeat_enable;
global uniform float race_time_elapsed;

void fragment() {

	if (UV2.y < -1.0 || UV2.y > 0.0){
		discard;
	}
	DEPTH = FRAGCOORD.z;
	//if (UV2.y < 1.0){
	vec3 ndc = vec3(SCREEN_UV * 2.0 - 1.0, DEPTH);
	vec4 view = INV_PROJECTION_MATRIX * vec4(ndc, 1.0);
	view.xyz /= view.w;
	float linear_depth = -view.z;
	if (race_time_elapsed <= 6.0){
		linear_depth -= 1.0;
	}else{
		linear_depth -= 0.01;
	}
	view.z = -linear_depth;
	view.xyz *= view.w;
	view = PROJECTION_MATRIX * view;
	DEPTH = view.z;

	vec2 base_uv = UV;
	vec4 albedo_tex = texture(texture_albedo,base_uv);
	ALBEDO = albedo_tex.rgb * 0.2;
	SPECULAR = 0.0;
}
