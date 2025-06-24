shader_type spatial;

render_mode unshaded, blend_mix, skip_vertex_transform, depth_test_disabled;

uniform vec4 albedo : source_color;
uniform sampler2D texture_albedo : source_color,filter_linear_mipmap,repeat_enable;
uniform sampler2D screen_texture : hint_screen_texture, repeat_disable, filter_linear_mipmap;
uniform sampler2D depth_texture : source_color, hint_depth_texture;
void vertex()
{
    mat4 modified_model_view = VIEW_MATRIX * mat4(
        INV_VIEW_MATRIX[0],
        INV_VIEW_MATRIX[1],
        INV_VIEW_MATRIX[2],
        MODEL_MATRIX[3]
    );
    MODELVIEW_MATRIX = modified_model_view;
	VERTEX = (VERTEX * MODEL_MATRIX[1].y) + (MODELVIEW_MATRIX * vec4(VERTEX, 1.0)).xyz;
	NORMAL = normalize((MODELVIEW_MATRIX * vec4(NORMAL, 0.0)).xyz);
}

void fragment() {
	vec3 particle_tex = texture(texture_albedo, UV).rgb;
	ALBEDO = texture(screen_texture, SCREEN_UV + vec2((particle_tex.r - 0.5) * -(SCREEN_UV.x - 0.5), (particle_tex.r - 0.5) * -(SCREEN_UV.y - 0.5)) * COLOR.r).rgb * 2.0;
	ALPHA = particle_tex.r * COLOR.a;
}
