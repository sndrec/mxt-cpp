shader_type spatial;

render_mode unshaded, blend_add, cull_disabled, skip_vertex_transform;

uniform float grid_size = 50.0;
uniform sampler2D grid_tex : filter_linear_mipmap_anisotropic;
varying vec3 world_normal;
varying vec3 world_pos;
varying vec2 UVX;
varying vec2 UVY;
varying vec2 UVZ;

float fresnel(float amount, vec3 normal, vec3 view)
{
	return pow((1.0 - clamp(dot(normalize(normal), normalize(view)), 0.0, 1.0 )), amount);
}

void vertex() {
	world_normal = NORMAL;
	world_pos = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz;
	UVZ = (world_pos).xy / grid_size;
	UVX = (world_pos).yz / grid_size;
	UVY = (world_pos).zx / grid_size;
	VERTEX = (MODELVIEW_MATRIX * vec4(VERTEX, 1.0)).xyz;
	NORMAL = normalize((MODELVIEW_MATRIX * vec4(NORMAL, 0.0)).xyz);
	BINORMAL = normalize((MODELVIEW_MATRIX * vec4(BINORMAL, 0.0)).xyz);
	TANGENT = normalize((MODELVIEW_MATRIX * vec4(TANGENT, 0.0)).xyz);
}

void fragment() {
	float x_factor = clamp(abs(world_normal.x) * 16.0 - 8.0, 0.0, 1.0);
	float y_factor = clamp(abs(world_normal.y) * 16.0 - 8.0, 0.0, 1.0);
	float z_factor = clamp(abs(world_normal.z) * 16.0 - 8.0, 0.0, 1.0);
	vec3 xy_tex = texture(grid_tex, UVZ).rgb;
	vec3 yz_tex = texture(grid_tex, UVX).rgb;
	vec3 zx_tex = texture(grid_tex, UVY).rgb;
	float dist_scale = max(0.0, 1.0 - (length(VERTEX) * 0.0008));
	float dot_x = abs(dot(CAMERA_DIRECTION_WORLD, vec3(1, 0, 0)));
	float dot_y = abs(dot(CAMERA_DIRECTION_WORLD, vec3(0, 1, 0)));
	float dot_z = abs(dot(CAMERA_DIRECTION_WORLD, vec3(0, 0, 1)));
	float lum_mult = 0.0;
	if (world_normal.x > 0.999 && dot_x > dot_y && dot_x > dot_z){
		lum_mult = 1.0;
	}
	if (world_normal.y > 0.999 && dot_y > dot_x && dot_y > dot_z){
		lum_mult = 1.0;
	}
	if (world_normal.z > 0.999 && dot_z > dot_x && dot_z > dot_y){
		lum_mult = 1.0;
	}
	vec3 final_texture_color = mix(mix(xy_tex, yz_tex, x_factor), zx_tex, y_factor);
	ALBEDO = vec3(final_texture_color * lum_mult * dist_scale * 0.25);
}
