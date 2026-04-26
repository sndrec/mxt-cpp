shader_type spatial;

render_mode unshaded, blend_mix, depth_draw_never, cull_disabled;
stencil_mode read, compare_not_equal, 1;

void vertex() {
	// Called for every vertex the material is visible on.
}

void fragment() {
	ALBEDO = vec3(0.0);
	ALPHA = 1.0;
	DEPTH = max(0.0, FRAGCOORD.z + 0.002);
}

//void light() {
//	// Called for every pixel for every light affecting the material.
//	// Uncomment to replace the default light processing function with this one.
//}
