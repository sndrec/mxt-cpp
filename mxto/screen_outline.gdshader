shader_type canvas_item;

render_mode blend_mix;

uniform sampler2D in_outline_screen_tex;
uniform sampler2D in_outline_mapped_tex : repeat_enable;

void vertex() {
	// Called for every vertex the material is visible on.
}

void fragment() {
	vec4 final_screen_tex = texture(in_outline_screen_tex, SCREEN_UV);
	float outline_alpha = final_screen_tex.r + 0.4;
	//COLOR.rgb = final_screen_tex.rgb;
	COLOR.rgb = final_screen_tex.rgb;
	//COLOR.a = final_screen_tex.a;
	COLOR.a = final_screen_tex.a;// * clamp(1.0 - outline_alpha, 0.0, 1.0);
	//COLOR.rgb = vec3(1.0, 1.0, 1.0);
}

//void light() {
//	// Called for every pixel for every light affecting the CanvasItem.
//	// Uncomment to replace the default light processing function with this one.
//}
