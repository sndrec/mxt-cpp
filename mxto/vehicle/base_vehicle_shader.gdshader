shader_type spatial;

render_mode ambient_light_disabled;//, skip_vertex_transform;

uniform sampler2D in_lightwarp : filter_nearest_mipmap_anisotropic, repeat_disable;
uniform sampler2D in_normal : filter_linear_mipmap_anisotropic, repeat_disable;
uniform sampler2D in_albedo : filter_linear_mipmap_anisotropic, repeat_disable;

void vertex() {
	TANGENT = vec3(1.0, 0.0, 0.0);
	BINORMAL = vec3(0.0, 0.0, -1.0);
	NORMAL = vec3(0.0, 1.0, 0.0);
}

float fresnel(float amount, vec3 normal, vec3 view)
{
	return pow((1.0 - clamp(dot(normalize(normal), normalize(view)), 0.0, 1.0 )), amount);
}

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
	vec3 normal_1 = texture(in_normal, UV + vec2(0.001, 0.002)).rgb * 2.0 - 1.0;
	vec3 normal_2 = texture(in_normal, UV + vec2(-0.002, 0.001)).rgb * 2.0 - 1.0;
	vec3 normal_3 = texture(in_normal, UV + vec2(0.002, -0.001)).rgb * 2.0 - 1.0;
	vec3 normal_4 = texture(in_normal, UV + vec2(-0.001, -0.002)).rgb * 2.0 - 1.0;
	vec3 normal_combined = normal_1 + normal_2 + normal_3 + normal_4;
	NORMAL = normalize(TANGENT * normal_combined.x + BINORMAL * normal_combined.y + NORMAL * normal_combined.z);
	vec3 in_colour = texture(in_albedo, UV).rgb;
	vec3 hsv_colour = rgb2hsv(in_colour);
	hsv_colour.y *= 1.3;
	vec3 final_colour = hsv2rgb(hsv_colour);
	ALBEDO = final_colour;// * fres;
	//DEPTH = VERTEX.z;
}

void light() {
	float light_val = (dot(NORMAL, LIGHT) * ATTENUATION) * 0.5 + 0.5;
	float fres = 1.0 - fresnel(1.0, NORMAL, VIEW);
	float fres_outline = fresnel(16.0, NORMAL, VIEW);
	light_val = clamp(light_val - fres * 0.1, 0.0, 1.0);
	light_val = clamp(light_val + fres_outline * 2.0, 0.0, 1.0);
	vec3  H		= normalize(LIGHT + VIEW);		// half-vector
	float ndoth	= max(dot(NORMAL, H), 0.0);
	float spec	= pow(ndoth, 8.0) * ATTENUATION;
	light_val = clamp(light_val + spec * 0.1, 0.0, 1.0);
	vec3 final_light_val = texture(in_lightwarp, vec2(light_val, 0.0f)).rgb;
	DIFFUSE_LIGHT += final_light_val * LIGHT_COLOR * 0.333;// * 0.3;
	//SPECULAR_LIGHT = vec3(0.0);
}
