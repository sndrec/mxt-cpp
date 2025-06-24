shader_type spatial;

uniform sampler2D treadmill_tex : source_color;

void fragment() {
	ALBEDO = texture(treadmill_tex, UV - vec2(0.0, TIME * 2.0)).rgb * 2.0;
}


