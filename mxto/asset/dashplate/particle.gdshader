shader_type spatial;

render_mode unshaded, blend_add;

uniform sampler2D spriteTexture : repeat_disable;

void fragment() {
	vec4 tex = texture(spriteTexture, vec2(1.0, 1.0) - UV.xy);
	ALBEDO = ((COLOR.rgb - vec3(0.5)) * 2.0);
	ALPHA = COLOR.a * tex.a;
}

//void light() {
	// Called for every pixel for every light affecting the material.
	// Uncomment to replace the default light processing function with this one.
//}
