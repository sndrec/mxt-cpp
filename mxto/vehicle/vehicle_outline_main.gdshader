shader_type spatial;

render_mode unshaded, skip_vertex_transform, cull_back, depth_draw_never, world_vertex_coords;
stencil_mode read, compare_not_equal, 1;

uniform float base_outline_width;
uniform vec3 outline_color : source_color;
global uniform float fog_dist;
const float OUTLINE_REFERENCE_PROJECTION_Y = 1.3763819; // cot(72 degrees / 2)

#include "res://asset/shader/ordered_dither.gdshaderinc"
varying flat vec2	v_line_o;			// screen-space origin  (0-1)
varying flat vec2	v_line_d;			// screen-space direction tail-o
varying float use_outline_width;
varying flat vec3 v_instance_velocity;

vec2 clip_to_uv(vec4 clip){				// clip -> (0-1) UV
	return clip.xy / clip.w * 0.5 + vec2(0.5);
}

void vertex() {
	vec3 in_velocity = INSTANCE_CUSTOM.xyz;
	v_instance_velocity = in_velocity;
	float fov_width_scale = OUTLINE_REFERENCE_PROJECTION_Y / max(abs(PROJECTION_MATRIX[1][1]), 0.0001);
	/* ---- screenspace line pre-compute --------------------------- */
	if (length(in_velocity) > 0.001)
	{
		vec3	world_o		= NODE_POSITION_WORLD;// + normalize(in_velocity) * 2.0;		// model origin in world
		vec3	world_t		= world_o + in_velocity + normalize(in_velocity) * 2.0;	// tail point

		vec4	clip_o		= PROJECTION_MATRIX * VIEW_MATRIX * vec4(world_o, 1.0);
		vec4	clip_t		= PROJECTION_MATRIX * VIEW_MATRIX * vec4(world_t, 1.0);

		v_line_o	= clip_to_uv(clip_o);
		v_line_d	= clip_to_uv(clip_t) - v_line_o;

		v_line_o *= VIEWPORT_SIZE;
		v_line_d *= VIEWPORT_SIZE;
		VERTEX = (MODELVIEW_MATRIX * vec4(VERTEX, 1.0)).xyz;
		NORMAL = normalize((MODELVIEW_MATRIX * vec4(NORMAL, 0.0)).xyz);
		BINORMAL = normalize((MODELVIEW_MATRIX * vec4(BINORMAL, 0.0)).xyz);
		TANGENT = normalize((MODELVIEW_MATRIX * vec4(TANGENT, 0.0)).xyz);

		use_outline_width = length(world_o - CAMERA_POSITION_WORLD) * base_outline_width * 0.1 * fov_width_scale;
		VERTEX += NORMAL * use_outline_width * 0.1;
		vec3 use_velocity = (VIEW_MATRIX * vec4(in_velocity, 0.0)).xyz;
		VERTEX += use_velocity * clamp(dot(NORMAL, use_velocity) * 1000000.0, 0.0, 1.0);
	}else{
		vec3	world_o		= NODE_POSITION_WORLD;
		VERTEX = (MODELVIEW_MATRIX * vec4(VERTEX, 1.0)).xyz;
		NORMAL = normalize((MODELVIEW_MATRIX * vec4(NORMAL, 0.0)).xyz);
		BINORMAL = normalize((MODELVIEW_MATRIX * vec4(BINORMAL, 0.0)).xyz);
		TANGENT = normalize((MODELVIEW_MATRIX * vec4(TANGENT, 0.0)).xyz);
		use_outline_width = length(world_o - CAMERA_POSITION_WORLD) * base_outline_width * 0.1 * fov_width_scale;
		VERTEX += NORMAL * use_outline_width * 0.1;
	}
}

void fragment() {
	vec3 in_velocity = v_instance_velocity;
	// param along the projected line, 0 at origin, 1 at tail
	if (length(in_velocity) < 0.001)
	{
		discard;
	}else{
		float coverage = track_distance_fade_coverage(VERTEX, fog_dist) * COLOR.a;
		float threshold = ordered_dither_threshold(FRAGCOORD, UV);
		threshold = mix(0.5, threshold, dither_strength);
		if (coverage < threshold) {
			discard;
		}
		vec2 scaled_suv = SCREEN_UV * VIEWPORT_SIZE;
		float len2	= max(dot(v_line_d, v_line_d), 1e-8);
		float t		= dot(scaled_suv - v_line_o, v_line_d) / len2;
		/*  v_line_d,  v_line_o, t  already defined exactly as in your shader */
		vec2 line_n		= v_line_d * inversesqrt(len2);		//   normalize
		vec2 perp_n		= vec2(-line_n.y, line_n.x);		//   90° CW

		/* signed distance from the pixel to the outline spine */
		float dist		= dot(scaled_suv - v_line_o, perp_n);

		/* outline half-width in screen UV units.
		   If you want it fully automatic, use the derivative of dist.
		   For stable results tie it to your use_outline_width:            */
		float half_w	= abs(dist) +               // a seed
						  length(fwidth(dist)) *	// ..pixel size
						  (1.0 / use_outline_width) * 10.0;

		/* x goes 0-1 across the ribbon, 0 on the mesh surface, 1 at the tip */
		float x			= dist / half_w + 0.75;

		/* final 2-D UV */
		vec2 uv			= vec2(x, t);
		ALBEDO = outline_color;
	}
}
