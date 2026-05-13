shader_type spatial;
render_mode cull_disabled, skip_vertex_transform;
uniform sampler2D road_tex : source_color, filter_linear_mipmap_anisotropic;
varying vec3 world_normal;
varying vec3 world_pos;
varying vec2 UVX;
varying vec2 UVY;
varying vec2 UVZ;

void vertex() {
	world_normal = (MODEL_MATRIX * vec4(NORMAL, 0.0)).xyz;
	world_pos = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz;
	UVZ = (world_pos).xy / 25.0;
	UVX = (world_pos).yz / 25.0;
	UVY = (world_pos).zx / 25.0;
	VERTEX = (MODELVIEW_MATRIX * vec4(VERTEX, 1.0)).xyz;
	NORMAL = normalize((MODELVIEW_MATRIX * vec4(NORMAL, 0.0)).xyz);
	BINORMAL = normalize((MODELVIEW_MATRIX * vec4(BINORMAL, 0.0)).xyz);
	TANGENT = normalize((MODELVIEW_MATRIX * vec4(TANGENT, 0.0)).xyz);
}

void fragment() {
	vec3 face_tint = vec3(0.6, 0.05, 0.05);
	if (FRONT_FACING){
		face_tint = vec3(1.0);
	}
	vec3 sampled_texture = texture(road_tex, UV).rgb;
	ALBEDO = sampled_texture * face_tint;
	EMISSION = sampled_texture * 0.08;
	ROUGHNESS = 0.75;
	METALLIC = 0.0;
	SPECULAR = 0.25;
}
