shader_type canvas_item;

uniform float fill_amount : hint_range(0.0, 1.0) = 1.0;
uniform vec4 cooldown_modulate : source_color = vec4(0.035, 0.035, 0.035, 1.0);

void fragment() {
	vec4 tex = texture(TEXTURE, UV) * COLOR;
	float fill_cutoff = 1.0 - clamp(fill_amount, 0.0, 1.0);
	float normal_region = step(fill_cutoff, UV.y);
	vec4 cooldown_tex = vec4(tex.rgb * cooldown_modulate.rgb, tex.a * cooldown_modulate.a);
	COLOR = mix(cooldown_tex, tex, normal_region);
}
