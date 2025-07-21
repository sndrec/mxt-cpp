shader_type canvas_item;

render_mode blend_add;

uniform sampler2D in_outline_screen_tex;
uniform sampler2D in_outline_mapped_tex : repeat_enable;

void vertex() {
	// Called for every vertex the material is visible on.
}

void fragment() {
	vec2 offsets[9] = vec2[](
		vec2(0.0, 0.0),				// center
		vec2(0.005, 0.0),
		vec2(-0.005, 0.0),
		vec2(0.0025, 0.00433),		// ~sin(60°)
		vec2(-0.0025, 0.00433),
		vec2(0.0025, -0.00433),
		vec2(-0.0025, -0.00433),
		vec2(0.0, 0.006),
		vec2(0.0, -0.006)
	);
	vec4 final_screen_tex = vec4(0.0);
	float weights[9] = float[](0.25, 0.125, 0.125, 0.1, 0.1, 0.1, 0.1, 0.05, 0.05);

	for (int i = 0; i < 9; i++) {
		final_screen_tex += texture(in_outline_screen_tex, SCREEN_UV + offsets[i]) * weights[i];
	}
	float outline_alpha = final_screen_tex.r + 0.4;
	//COLOR.rgb = final_screen_tex.rgb;
	COLOR.rgb = vec3(0.5, 0.05, 0.15);
	//COLOR.a = final_screen_tex.a;
	COLOR.a = final_screen_tex.a * clamp(1.0 - outline_alpha, 0.0, 1.0);
	//COLOR.rgb = vec3(1.0, 1.0, 1.0);
}

//void light() {
//	// Called for every pixel for every light affecting the CanvasItem.
//	// Uncomment to replace the default light processing function with this one.
//}
