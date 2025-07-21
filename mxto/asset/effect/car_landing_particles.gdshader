shader_type spatial;

render_mode unshaded, blend_add, depth_draw_never;

uniform sampler2D landing_tex : source_color;

void fragment() {
	ALBEDO = texture(landing_tex, vec2(UV.x, -UV.y)).rgb * 0.35 * COLOR.rgb;
}

//void light() {
	// Called for every pixel for every light affecting the material.
	// Uncomment to replace the default light processing function with this one.
//}
