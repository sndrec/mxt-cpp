shader_type canvas_item;

uniform float health_amount = 100.0;
uniform float max_health_amount = 100.0;
uniform bool can_boost = false;
uniform float health_to_deplete = 0.0;
uniform float boost_energy_cost = 15.0;

vec3 rgb2hsv(vec3 c)
{
    vec4 K = vec4(0.0, -1.0 / 3.0, 2.0 / 3.0, -1.0);
    vec4 p = mix(vec4(c.bg, K.wz), vec4(c.gb, K.xy), step(c.b, c.g));
    vec4 q = mix(vec4(p.xyw, c.r), vec4(c.r, p.yzx), step(p.x, c.r));

    float d = q.x - min(q.w, q.y);
    float e = 1.0e-10;
    return vec3(abs(q.z + (q.w - q.y) / (6.0 * d + e)), d / (q.x + e), q.x);
}

vec3 hsv2rgb(vec3 c)
{
    vec4 K = vec4(1.0, 2.0 / 3.0, 1.0 / 3.0, 3.0);
    vec3 p = abs(fract(c.xxx + K.xyz) * 6.0 - K.www);
    return c.z * mix(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);
}

void fragment() {
	vec2 use_uv = UV;
	use_uv.x += UV.y * -0.05;
	use_uv.x *= 1.05;
	if (use_uv.x < 0.0){
		discard;
	};
	float dist_from_middle = distance(0.5, use_uv.y);
	float energy_position = use_uv.x * max_health_amount;
	float segment_anchor = health_amount - health_to_deplete;
	float segment_phase = fract((segment_anchor - energy_position) / max(boost_energy_cost, 0.001));
	float segment_val = distance(segment_phase, 0.5);
	float seg_mix = max(dist_from_middle, segment_val);
	seg_mix = pow(seg_mix * 2.0, 16.0);
	vec2 mini_uv = use_uv;
	mini_uv.x = segment_phase;
	float diff_mix = seg_mix + (mini_uv.x - mini_uv.y) * 0.4;
	vec3 rainbow = hsv2rgb(vec3(use_uv.x + diff_mix * 0.2, 1.0, 1.0)) * (1.0 - seg_mix * 0.4) - seg_mix * 0.2;
	float cutoff = clamp((health_amount / max_health_amount - use_uv.x) * 512.0, 0.0, 1.0);
	float deplete_cutoff = clamp(((health_amount - health_to_deplete) / max_health_amount - use_uv.x) * 512.0, 0.0, 1.0);
	vec4 bar_color = mix(vec4(1.0, 0.8, 0.9, 1.0), vec4(rainbow, 1.0), deplete_cutoff);
	vec4 empty = vec4(0.0, 0.0, 0.0, 0.25);
	vec4 final_color = mix(empty, vec4(0.6, 0.9, 0.6, 1.0), cutoff);
	if (can_boost){
		final_color = mix(empty, bar_color, cutoff);
	}
	vec2 border_width = 2.0 * fwidth(UV);
	float border = max(
		max(1.0 - step(border_width.x, use_uv.x), 1.0 - step(border_width.x, 1.0 - UV.x)),
		max(1.0 - step(border_width.y, UV.y), 1.0 - step(border_width.y, 1.0 - UV.y))
	);
	vec4 meter_color = final_color + vec4(vec3(clamp(pow(dist_from_middle, 2.0), 0.0, 1.0)), 0.0);
	COLOR = mix(meter_color, vec4(1.0), border);
}
