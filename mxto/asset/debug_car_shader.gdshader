// NOTE: Shader automatically converted from Godot Engine 4.4.1.rc1's StandardMaterial3D.

shader_type spatial;
render_mode blend_mix, depth_draw_always, cull_back, diffuse_burley, specular_schlick_ggx;

uniform vec4 albedo : source_color;
uniform float shininess		: hint_range(1.0, 256.0) = 32.0;	// higher = tighter highlight
uniform float spec_strength	: hint_range(0.0, 10.0) = 2.0;	// overall multiplier

void fragment() {
	ALBEDO = albedo.rgb;
	ROUGHNESS = 1.0;
}

void light() {
    vec3 N = normalize(NORMAL);
    vec3 L = normalize(LIGHT);  // view-space light dir
    vec3 V = normalize(VIEW);   // view-space view dir

    // --- Diffuse (Lambert) ---
    float ndotl = max(dot(N, L), 0.0);
    DIFFUSE_LIGHT += ndotl * LIGHT_COLOR * ATTENUATION / PI; // normalized energy :contentReference[oaicite:1]{index=1}

    // --- Specular (Blinn-Phong) ---
    vec3 H = normalize(L + V);
    float ndoth = max(dot(N, H), 0.0);
		float spec = min(pow(ndoth, shininess) * spec_strength, 0.5);
    DIFFUSE_LIGHT += spec * LIGHT_COLOR * ATTENUATION;
}