shader_type canvas_item;

uniform float health_amount = 100.0;
uniform float max_health_amount = 100.0;
uniform bool can_boost = false;
uniform float health_to_deplete = 0.0;
uniform float boost_energy_cost = 15.0;
uniform sampler2D boost_available_gradient : source_color, filter_linear, repeat_disable;

void fragment() {
	const float meter_skew = 0.05;
	vec2 use_uv = UV;
	use_uv.x = (UV.x - UV.y * meter_skew) / (1.0 - meter_skew);
	float dist_from_middle = distance(0.5, use_uv.y);
	float energy_position = use_uv.x * max_health_amount;
	float segment_anchor = health_amount - health_to_deplete;
	float segment_phase = fract((segment_anchor - energy_position) / max(boost_energy_cost, 0.001));
	float segment_val = distance(segment_phase, 0.5);
	float seg_mix = max(dist_from_middle, segment_val);
	seg_mix = pow(seg_mix * 2.0, 16.0);
	vec2 segment_uv = vec2(segment_phase, use_uv.y);
	float gradient_distortion = seg_mix + (segment_uv.x - segment_uv.y) * 0.4;
	float gradient_x = clamp(UV.x + gradient_distortion * 0.2, 0.0, 1.0);
	vec3 boost_available_color = texture(boost_available_gradient, vec2(gradient_x, 0.0)).rgb;
	boost_available_color = boost_available_color * (1.0 - seg_mix * 0.4) - seg_mix * 0.2;
	float cutoff = clamp((health_amount / max_health_amount - use_uv.x) * 512.0, 0.0, 1.0);
	float deplete_cutoff = clamp(((health_amount - health_to_deplete) / max_health_amount - use_uv.x) * 512.0, 0.0, 1.0);
	vec4 bar_color = mix(vec4(1.0, 0.8, 0.9, 1.0), vec4(boost_available_color, 1.0), deplete_cutoff);
	vec4 empty = vec4(0.0, 0.0, 0.0, 0.25);
	vec4 final_color = mix(empty, vec4(0.6, 0.9, 0.6, 1.0), cutoff);
	if (can_boost){
		final_color = mix(empty, bar_color, cutoff);
	}
	vec2 edge_pixel_width = max(fwidth(use_uv), vec2(0.000001));
	float edge_distance_pixels = min(
		min(use_uv.x / edge_pixel_width.x, (1.0 - use_uv.x) / edge_pixel_width.x),
		min(use_uv.y / edge_pixel_width.y, (1.0 - use_uv.y) / edge_pixel_width.y)
	);
	float shape_coverage = smoothstep(-0.5, 0.5, edge_distance_pixels);
	float border_coverage = 1.0 - smoothstep(1.5, 2.5, edge_distance_pixels);
	vec4 meter_color = final_color + vec4(vec3(clamp(pow(dist_from_middle, 2.0), 0.0, 1.0)), 0.0);
	COLOR = mix(meter_color, vec4(1.0), border_coverage);
	COLOR.a *= shape_coverage;
}
