shader_type spatial;

render_mode cull_disabled;

uniform sampler2D in_tex;

void fragment() { 
	vec3 color = mix(vec3(0.25, 1.0, 0.25), vec3(1.0, 0.25, 0.25), float(FRONT_FACING));
	vec4 tex = texture(in_tex, UV * vec2(1.0, -0.05), -2.0);
	ALBEDO = color + tex.rgb;
	EMISSION = tex.rgb * 0.2f;
}

//void light() {
//	// Called for every pixel for every light affecting the material.
//	// Uncomment to replace the default light processing function with this one.
//}
