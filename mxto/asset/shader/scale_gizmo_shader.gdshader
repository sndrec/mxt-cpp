shader_type spatial;

render_mode unshaded, depth_test_disabled;

uniform bool x_moused = false;
uniform bool x_left_moused = false;
uniform bool x_right_moused = false;
uniform bool y_moused = false;
uniform bool clicking = false;

uniform sampler2D gizmo_colour : source_color;

void fragment() {
	vec3 use_color = texture(gizmo_colour, UV).rgb;
	vec3 mouse_color = vec3(1, 1, 1);
	if (clicking){
		mouse_color = vec3(1, 1, 0);
	}
	if (x_moused && UV.x < 0.5 && UV.y < 0.5){
		use_color = mix(use_color, mouse_color, 0.25);
	}
	if (x_left_moused && UV.x < 0.5 && UV.y < 0.0){
		use_color = mix(use_color, mouse_color, 0.25);
	}
	if (x_right_moused && UV.x > 1.0 && UV.y > 0.5){
		use_color = mix(use_color, mouse_color, 0.25);
	}
	if (y_moused && UV.x > 0.5 && UV.y < 0.5){
		use_color = mix(use_color, mouse_color, 0.25);
	}
	ALBEDO = use_color;
}