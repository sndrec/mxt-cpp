shader_type canvas_item;

uniform float health_amount = 100.0;
uniform float max_health_amount = 100.0;
uniform bool can_boost = false;
uniform float health_to_deplete = 0.0;

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
	vec3 rainbow = hsv2rgb(vec3(UV.x, 1.0, 1.0));
	float dist_from_middle = distance(0.5, UV.y);
	float segment_val = clamp((sin(max_health_amount * UV.x * 0.5 + PI * 0.5) - 0.95) * 5.0, 0.0, 1.0) * dist_from_middle * 5.0;
	vec4 segments = vec4(segment_val, segment_val, segment_val, 0.0);
	float cutoff = clamp((health_amount / max_health_amount - UV.x) * 512.0, 0.0, 1.0);
	float deplete_cutoff = clamp(((health_amount - health_to_deplete) / max_health_amount - UV.x) * 512.0, 0.0, 1.0);
	vec4 bar_color = mix(vec4(1.0, 0.8, 0.9, 1.0), vec4(rainbow, 1.0), deplete_cutoff);
	vec4 empty = vec4(0.0, 0.0, 0.0, 0.2);
	vec4 final_color = mix(empty, vec4(0.6, 0.9, 0.6, 1.0), cutoff);
	if (can_boost){
		final_color = mix(empty, bar_color, cutoff);
	}
	COLOR = final_color + segments + vec4(vec3(clamp(pow(dist_from_middle, 2.0), 0.0, 1.0)), 0.0);
}
