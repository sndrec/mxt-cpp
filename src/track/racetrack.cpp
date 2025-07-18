#include "track/racetrack.h"
#include "car/physics_car.h" // for CollisionData and RoadData
#include <cfloat>
#include <algorithm>
#include "godot_cpp/variant/utility_functions.hpp"
#include "mxt_core/curve.h"
#include "mxt_core/enums.h"
#include <queue>
#include <vector>
#include <limits>
#include "mxt_core/debug.hpp"

int RaceTrack::find_checkpoint_recursive(const godot::Vector3 &pos, int cp_index, int iterations)
{
	if (cp_index == -1)
	{
		return get_best_checkpoint(pos);
	}
	if (iterations > 10)
		return -1;
	const CollisionCheckpoint &cp = checkpoints[cp_index];
	if (!cp.end_plane.is_point_over(pos) && cp.start_plane.is_point_over(pos))
		return cp_index;
	for (int i = 0; i < cp.num_neighboring_checkpoints; ++i) {
		int neighbor = cp.neighboring_checkpoints[i];
		int found = find_checkpoint_recursive(pos, neighbor, iterations + 1);
		if (found != -1)
			return found;
	}
	return -1;
}

void RaceTrack::get_road_surface(int cp_idx, const godot::Vector3 &point,
								  godot::Vector2 &road_t, godot::Vector3 &spatial_t, godot::Transform3D &out_transform, bool oriented)
{
	if (cp_idx == -1)
	{
		road_t.x = -1000.0f;
		return;
	}
	CollisionCheckpoint *cp = &checkpoints[cp_idx];
	if (cp->end_plane.is_point_over(point) || !cp->start_plane.is_point_over(point))
	{
		int new_idx = get_best_checkpoint(point, cp_idx);
		if (new_idx != -1)
		{
			cp_idx = new_idx;
			cp = &checkpoints[cp_idx];
		}
	}
	godot::Vector3 p1 = cp->start_plane.project(point);
	godot::Vector3 p2 = cp->end_plane.project(point);
	float cp_t = get_closest_t_on_segment(point, p1, p2);
	cp_t = std::clamp(cp_t, 0.0f, 1.0f);
       godot::Basis basis;
       basis[0] = cp->orientation_start[0].lerp(cp->orientation_end[0], cp_t);
       basis[2] = cp->orientation_start[2].lerp(cp->orientation_end[2], cp_t);
       basis[1] = cp->orientation_start[1].lerp(cp->orientation_end[1], cp_t);
	godot::Vector3 midpoint = cp->position_start.lerp(cp->position_end, cp_t);
	godot::Plane sep_x_plane(basis[0], midpoint);
	godot::Plane sep_y_plane(basis[1], midpoint);
	float x_r = lerp(cp->x_radius_start_inv, cp->x_radius_end_inv, cp_t);
	float y_r = lerp(cp->y_radius_start_inv, cp->y_radius_end_inv, cp_t);
	float tx = sep_x_plane.distance_to(point) * x_r;
	float ty = sep_y_plane.distance_to(point) * y_r;
	float tz = remap_float(cp_t, 0.0f, 1.0f, cp->t_start, cp->t_end);
	spatial_t = godot::Vector3(tx, ty, tz);
	bool y_less_than_x = y_r > 0.5f;
	bool is_open = false;
	bool use_top_half = false;

	// Check for open road shape
	RoadShape *shape = segments[cp->road_segment].road_shape;
	if (shape->shape_type == ROAD_SHAPE_TYPE::ROAD_SHAPE_CYLINDER_OPEN) {
		is_open = true;
		use_top_half = true;
	} else if (shape->shape_type == ROAD_SHAPE_TYPE::ROAD_SHAPE_PIPE_OPEN) {
		is_open = true;
		use_top_half = false;
	}

	if (is_open && y_less_than_x) {
		float openness = shape->openness->sample(tz);
		if (openness <= 0.50001f) {
			float tx_clamped = std::clamp(tx, -1.0f, 1.0f);
			float y_val = sqrtf(1.0f - tx_clamped * tx_clamped);
			if (!use_top_half)
				y_val = -y_val;
			spatial_t.y = y_val;
		}
	}
	segments[cp->road_segment].road_shape->find_t_from_relative_pos(road_t, spatial_t);
	segments[cp->road_segment].road_shape->get_oriented_transform_at_time(out_transform, road_t);
}

static void convert_point_to_road(RaceTrack *track, int cp_idx, const godot::Vector3 &point,
								  godot::Vector2 &road_t, godot::Vector3 &spatial_t, float *out_cp_t = nullptr)
{
	if (cp_idx == -1)
	{
		return;
	}
	const CollisionCheckpoint *cp = &track->checkpoints[cp_idx];

	godot::Vector3 p1 = cp->start_plane.project(point);
	godot::Vector3 p2 = cp->end_plane.project(point);
	float cp_t = get_closest_t_on_segment(point, p1, p2);
	if (out_cp_t)
		*out_cp_t = cp_t;

       godot::Basis basis;
       basis[0] = cp->orientation_start[0].lerp(cp->orientation_end[0], cp_t);
       basis[2] = cp->orientation_start[2].lerp(cp->orientation_end[2], cp_t);
       basis[1] = cp->orientation_start[1].lerp(cp->orientation_end[1], cp_t);

	godot::Vector3 midpoint = cp->position_start.lerp(cp->position_end, cp_t);
	godot::Plane sep_x_plane(basis[0], midpoint);
	godot::Plane sep_y_plane(basis[1], midpoint);

	float x_r = lerp(cp->x_radius_start_inv, cp->x_radius_end_inv, cp_t);
	float y_r = lerp(cp->y_radius_start_inv, cp->y_radius_end_inv, cp_t);

	float tx = sep_x_plane.distance_to(point) * x_r;
	float ty = sep_y_plane.distance_to(point) * y_r;
	float tz = remap_float(cp_t, 0.0f, 1.0f, cp->t_start, cp->t_end);

	spatial_t = godot::Vector3(tx, ty, tz);

	RoadShape *shape = track->segments[cp->road_segment].road_shape;

	bool y_less_than_x = y_r > 0.5f;
	bool is_open = false;
	bool use_top_half = false;

	// Check for open road shape
	if (shape->shape_type == ROAD_SHAPE_TYPE::ROAD_SHAPE_CYLINDER_OPEN) {
		is_open = true;
		use_top_half = true;
	} else if (shape->shape_type == ROAD_SHAPE_TYPE::ROAD_SHAPE_PIPE_OPEN) {
		is_open = true;
		use_top_half = false;
	}
	//DEBUG::disp_text("spatial_t_old", spatial_t);
	//DEBUG::disp_text("y_r", y_r);
	//DEBUG::disp_text("x_r", x_r);
	//DEBUG::disp_text("is_open", is_open);
	//DEBUG::disp_text("use_top_half", use_top_half);

	if (is_open && y_less_than_x) {
		float openness = shape->openness->sample(tz);
		//DEBUG::disp_text("openness", openness);
		if (openness <= 0.50001f) {
			float tx_clamped = std::clamp(tx, -1.0f, 1.0f);
			//DEBUG::disp_text("tx_clamped", tx_clamped);
			float y_val = sqrtf(1.0f - tx_clamped * tx_clamped);
			//DEBUG::disp_text("y_val", y_val);
			if (!use_top_half)
				y_val = -y_val;
			spatial_t.y = y_val;
		}
	}
	//DEBUG::disp_text("spatial_t", spatial_t);

	shape->find_t_from_relative_pos(road_t, spatial_t);
}


struct CastParams {
	RaceTrack *track;
	uint8_t mask;
};

// ──────────────────────────────────────────────────────────────────────────────
// Same helpers / includes / forward-decls as before …
// ──────────────────────────────────────────────────────────────────────────────

static void cast_segment_fast(const CastParams  &params,
	CollisionData                               &out_collision,
	godot::Vector3 const                        &p0,
	godot::Vector3 const                        &p1,
	int                                         use_idx,
	godot::Vector3 const                        &sample_pt, // NEW
	bool                                        oriented    = true)
{
	out_collision.collided          = false;
	out_collision.road_data.cp_idx  = -1;
	if ((params.mask & CAST_FLAGS::WANTS_TRACK) == 0)
		return;

	if (use_idx == -1)
	{
		return;
	}

	RaceTrack *track                = params.track;
	const TrackSegment &segment     = track->segments[track->checkpoints[use_idx].road_segment];

	godot::Object* dd3d;
	if (DEBUG::dip_enabled(DIP_SWITCH::DIP_DRAW_RAYCASTS)){
		dd3d = godot::Engine::get_singleton()->get_singleton("DebugDraw3D");
		dd3d->call("draw_arrow", p0, p1, godot::Color(1.0f, 1.0f, 1.0f), 0.25, true, _TICK_DELTA);
	}

	godot::Vector2  road_t_sample_raw;  godot::Vector3 spatial_t_sample;
	convert_point_to_road(track, use_idx, sample_pt, road_t_sample_raw, spatial_t_sample);

	godot::Transform3D surf;        // THE ONLY SURFACE FETCH
	//if (oriented)
	segment.road_shape->get_oriented_transform_at_time(surf, road_t_sample_raw);
	//else
		//segment.road_shape->get_transform_at_time(surf, road_t_sample_raw);

	const godot::Vector3 surf_n     = surf.basis[1];                    // Up/normal
	const godot::Vector3 surf_fwd   = surf.basis[2];                    // Forward (needed for rails)

	// ── 2) Basic plane hit against the single surface ────────────────────────
	const godot::Vector3 ray        = p1 - p0;
	float best_t                    = FLT_MAX;

	const float d0                  = (p0 - surf.origin).dot(surf_n);
	const float d1                  = (p1 - surf.origin).dot(surf_n);

	if (!((d0 <= 0.0f && d1 <= 0.0f) || (d0 >= 0.0f && d1 >= 0.0f))) {
		const float t = d0 / (d0 - d1);                                     // p0->p1 crossing %
		if (t >= 0.0f && t <= 1.0f) {
			const godot::Vector3 hit_point  = p0 + ray * t;

			godot::Vector2 road_t_hit_raw;  godot::Vector3 spatial_t_hit;
			convert_point_to_road(track, use_idx, hit_point, road_t_hit_raw, spatial_t_hit);

			if ((road_t_hit_raw.x <= 1.0f && road_t_hit_raw.x > -1.0f) && ((params.mask & CAST_FLAGS::WANTS_BACKFACE) != 0 || ray.dot(surf_n) <= 0.0f)) {
				const float dist = t * ray.length();
				best_t                      = dist;
				out_collision.collided      = true;
				out_collision.collision_point   = hit_point;
				out_collision.collision_normal  = surf_n;

				out_collision.road_data.cp_idx          = use_idx;
				out_collision.road_data.spatial_t       = spatial_t_hit;
				out_collision.road_data.road_t          = road_t_hit_raw;
				out_collision.road_data.closest_surface = surf;     // reuse single transform
				segment.curve_matrix->sample(out_collision.road_data.closest_root, road_t_hit_raw.y);

				out_collision.road_data.terrain = 0;
				if (params.mask & CAST_FLAGS::WANTS_TERRAIN) {
					for (int i = 0; i < segment.road_shape->num_embeds; ++i) {
						RoadEmbed *embed = &segment.road_shape->road_embeds[i];
						if (road_t_hit_raw.y > embed->start_offset && road_t_hit_raw.y < embed->end_offset) {
							const float l   = embed->left_border->sample(road_t_hit_raw.y);
							const float r   = embed->right_border->sample(road_t_hit_raw.y);
							//DEBUG::disp_text("embed_l", l);
							//DEBUG::disp_text("embed_r", r);
							//DEBUG::disp_text("road_t_x", road_t_hit_raw.x);
							if (road_t_hit_raw.x > l && road_t_hit_raw.x < r) {
								//DEBUG::disp_text("terrain", out_collision.road_data.terrain);
								out_collision.road_data.terrain = embed->embed_type;
								break;
							}
						}
					}
				}
			}
		}
	}

	if (DEBUG::dip_enabled(DIP_SWITCH::DIP_DRAW_RAYCASTS)){
		if (out_collision.collided){
			dd3d->call("draw_arrow", out_collision.collision_point, out_collision.collision_point + out_collision.collision_normal * 2, godot::Color(0.0f, 0.0f, 1.0f), 0.25, true, _TICK_DELTA);
		}
	}

	// ── 3) Optional rail cast (uses the SAME surface data) ───────────────────
	if ((params.mask & CAST_FLAGS::WANTS_RAIL) == 0)
		return;

	if (segment.road_shape->shape_type == ROAD_SHAPE_TYPE::ROAD_SHAPE_PIPE ||
		segment.road_shape->shape_type == ROAD_SHAPE_TYPE::ROAD_SHAPE_CYLINDER ||
		segment.road_shape->shape_type == ROAD_SHAPE_TYPE::ROAD_SHAPE_PIPE_OPEN ||
		segment.road_shape->shape_type == ROAD_SHAPE_TYPE::ROAD_SHAPE_CYLINDER_OPEN)
		return;

	RoadTransform root_t;
	segment.curve_matrix->sample(root_t, road_t_sample_raw.y);
	const godot::Basis rbasis       = root_t.t3d.basis;
	const godot::Vector3 up_normal = rbasis.get_column(1);
	const godot::Vector3 side_dir = rbasis.get_column(0);
	const godot::Vector3 side_scaled = side_dir * root_t.scale.x;
	const godot::Vector3 left_pos   = root_t.t3d.origin + side_scaled;
	const godot::Vector3 right_pos  = root_t.t3d.origin - side_scaled;
	const godot::Vector3 left_plane_n   = -side_dir;
	const godot::Vector3 right_plane_n  =  side_dir;

	struct RailSide { godot::Vector3 pos, plane_n, rail_n; float height; };
	const RailSide sides[2] = {
		{ left_pos,  left_plane_n,  -surf_n.cross(surf_fwd),    segment.left_rail_height    },
		{ right_pos, right_plane_n,  surf_n.cross(surf_fwd),    segment.right_rail_height   }
	};

	for (const RailSide &side : sides) {
		const float ra = (p0 - side.pos).dot(side.plane_n);
		const float rb = (p1 - side.pos).dot(side.plane_n);
		if ((ra <= 0.0f && rb <= 0.0f) || (ra >= 0.0f && rb >= 0.0f))
			continue;

		const float t = ra / (ra - rb);
		if (t < 0.0f || t > 1.0f)
			continue;

		const godot::Vector3 hit = p0 + ray * t;

		godot::Vector2 road_t_hit_raw;  godot::Vector3 spatial_t_hit;
		convert_point_to_road(track, use_idx, hit, road_t_hit_raw, spatial_t_hit);

		const float vdist = (hit - surf.origin).dot(surf_n);        // height above track
		if (vdist < 0.0f || vdist > side.height * root_t.scale.y)
			continue;
		if ((params.mask & CAST_FLAGS::WANTS_BACKFACE) == 0 && ray.dot(side.rail_n) > 0.0f)
			continue;

		const float dist = t * ray.length();
		if (dist < best_t) {
			best_t                          = dist;
			out_collision.collided          = true;
			out_collision.collision_point   = hit;
			out_collision.collision_normal  = side.rail_n;

			out_collision.road_data.cp_idx          = use_idx;
			out_collision.road_data.spatial_t       = spatial_t_hit;
			out_collision.road_data.road_t          = road_t_hit_raw;
			out_collision.road_data.closest_surface = surf; // reuse same transform
			segment.curve_matrix->sample(out_collision.road_data.closest_root, road_t_hit_raw.y);
			out_collision.road_data.terrain         = 0x100;
		}
	}

	if (DEBUG::dip_enabled(DIP_SWITCH::DIP_DRAW_RAYCASTS)){
		if (out_collision.collided && out_collision.road_data.terrain == 0x100){
			dd3d->call("draw_arrow", out_collision.collision_point, out_collision.collision_point + out_collision.collision_normal * 2, godot::Color(1.0f, 0.0f, 0.0f), 0.25, true, _TICK_DELTA);
		}
	}
}

void RaceTrack::cast_vs_track_fast(CollisionData &out_collision,
	godot::Vector3 const &p0,
	godot::Vector3 const &p1,
	uint8_t mask,
	int start_idx, bool oriented)
{
	out_collision.collided = false;
	out_collision.road_data.cp_idx = -1;

	if ((mask & CAST_FLAGS::WANTS_TRACK) == 0)
		return;

	if (start_idx == -1)
	{
		return;
	}

	// choose sample point
	godot::Vector3 sample_point;
	if (mask & CAST_FLAGS::SAMPLE_FROM_P0)
		sample_point = p0;
	else if (mask & CAST_FLAGS::SAMPLE_FROM_MID)
		sample_point = (p0 + p1) * 0.5f;
	else
		sample_point = p1;
	
	CastParams params{ this, mask };
	cast_segment_fast(params, out_collision, p0, p1, start_idx, sample_point, true);
}
