shader_type spatial;

render_mode unshaded, blend_add, shadows_disabled;

uniform vec3 albedo : source_color;

void vertex() {
	// Called for every vertex the material is visible on.
}

void fragment() {
	ALPHA = 1.0;
	ALBEDO = (albedo * 2.0) - vec3(0.5);
}

void light() {
	// Called for every pixel for every light affecting the material.
}
