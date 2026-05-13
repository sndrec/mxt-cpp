shader_type spatial;

render_mode unshaded, depth_test_disabled, cull_disabled;

uniform bool x_moused = false;
uniform bool y_moused = false;
uniform bool z_moused = false;
uniform bool clicking = false;

uniform sampler2D gizmo_colour : source_color, filter_nearest;

void fragment() {
	vec3 use_color = texture(gizmo_colour, UV).rgb;
	vec3 mouse_color = vec3(1, 1, 1);
	if (clicking){
		mouse_color = vec3(1, 1, 0);
	}
	if (x_moused && UV.x < 0.5 && UV.y < 0.5){
		use_color = mix(use_color, mouse_color, 0.25);
	}
	if (y_moused && UV.x > 0.5 && UV.y < 0.5){
		use_color = mix(use_color, mouse_color, 0.25);
	}
	if (z_moused && UV.x < 0.5 && UV.y > 0.5){
		use_color = mix(use_color, mouse_color, 0.5);
	}
	ALBEDO = use_color;
}
