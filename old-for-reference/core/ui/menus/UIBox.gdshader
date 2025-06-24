shader_type canvas_item;

uniform vec2 box_size = vec2(200, 200);
uniform float border_size = 32.0;
uniform float noise = 0.2;
uniform float border_uv_scale = 1.0;
uniform float noise_uv_scale = 1.0;
uniform bool bg_random_scroll = false;
uniform vec2 bg_scroll_speed = vec2(0.0, 0.0);
uniform sampler2D border_texture : repeat_enable;
uniform sampler2D bg_texture : repeat_enable;

float random (vec2 st) {
    return fract(sin(dot(st.xy,
                         vec2(12.9898,78.233)))*
        43758.5453123);
}

void fragment() {
	vec2 modified_uv = UV * box_size;
	vec2 modified_uv_2 = (vec2(1.0, 1.0) - UV) * box_size;
	float dist_x = min(distance(modified_uv.x, 0), distance(modified_uv.x, box_size.x));
	float dist_y = min(distance(modified_uv.y, 0), distance(modified_uv.y, box_size.y));
	float dist = min(min(dist_x, dist_y) / border_size, 1.0);
	vec4 border_color = texture(border_texture, vec2(border_uv_scale, 1.0) * vec2(max(min(modified_uv.x, modified_uv_2.x) / (border_size * 16.0), min(modified_uv.y, modified_uv_2.y) / (border_size * 16.0)), dist));
	dist = pow(dist, 32.0);
	float use_time = TIME - mod(TIME, 0.016666666);
	float rnd = random( vec2(use_time * 0.5348583, use_time * 1.23823495) );
	float rnd2 = random( vec2(use_time * 1.2323546, use_time * 0.2342567) );
	vec2 use_scroll = vec2(rnd, rnd2);
	if (!bg_random_scroll) {
		use_scroll = bg_scroll_speed * TIME;
	}
	COLOR = mix(texture(bg_texture, modified_uv * noise_uv_scale * 0.0025 + use_scroll) * vec4(1.0, 1.0, 1.0, noise), border_color, 1.0 - dist);
}

void light() {
	// Called for every pixel for every light affecting the CanvasItem.
}
