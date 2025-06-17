#pragma once

#include "track/track_segment.h"
#include "track/collision_checkpoint.h"
#include "mxt_core/math_utils.h"
#include "godot_cpp/variant/aabb.hpp"
#include "track/checkpoint_bvh.h"
#include <vector>
#include <algorithm>

struct CollisionData;

class RaceTrack
{
public:
        int num_segments;
        int num_checkpoints;
        TrackSegment* segments;
        CollisionCheckpoint* checkpoints;
        std::vector<godot::AABB> checkpoint_aabbs;
        CheckpointBVH checkpoint_bvh;
        int find_checkpoint_recursive(const godot::Vector3 &pos, int cp_index, int iterations = 0) const;
        int find_checkpoint_bfs(const godot::Vector3 &pos, int start_index) const;
        void cast_vs_track(CollisionData &out_collision, const godot::Vector3 &p0, const godot::Vector3 &p1, uint8_t mask, int start_idx = -1, bool oriented = true);
        void cast_vs_track_fast(CollisionData &out_collision, const godot::Vector3 &p0, const godot::Vector3 &p1, uint8_t mask, int start_idx = -1, bool oriented = false);
        void get_road_surface(int cp_idx, const godot::Vector3 &point, godot::Vector2 &road_t, godot::Vector3 &spatial_t, godot::Transform3D &out_transform, bool oriented = true);
        void build_checkpoint_bvh();
        void debug_draw_checkpoint_bvh() const;
        std::vector<int> get_viable_checkpoints(godot::Vector3 in_point)
        {
                std::vector<int> return_checkpoints;
                return_checkpoints.reserve(16);

                if (!checkpoint_bvh.nodes.empty()) {
                        checkpoint_bvh.query(0, in_point, return_checkpoints);
                } else {
                        for (int i = 0; i < num_checkpoints; i++)
                                return_checkpoints.push_back(i);
                }

                // additional plane checks
                return_checkpoints.erase(std::remove_if(return_checkpoints.begin(), return_checkpoints.end(), [&](int idx){
                        return !checkpoints[idx].start_plane.is_point_over(in_point) || checkpoints[idx].end_plane.is_point_over(in_point);
                }), return_checkpoints.end());
                return return_checkpoints;
        }
        int get_best_checkpoint(godot::Vector3 in_point)
        {
                std::vector<int> candidates;
                candidates.reserve(16);

                if (!checkpoint_bvh.nodes.empty()) {
                        checkpoint_bvh.query(0, in_point, candidates);
                } else {
                        for (int i = 0; i < num_checkpoints; i++)
                                candidates.push_back(i);
                }

                candidates.erase(std::remove_if(candidates.begin(), candidates.end(), [&](int idx){
                        return !checkpoints[idx].start_plane.is_point_over(in_point) || checkpoints[idx].end_plane.is_point_over(in_point);
                }), candidates.end());
                int   best_cp     = -1;
                float best_dist2  = std::numeric_limits<float>::infinity();
                for (int idx : candidates) {
                        const CollisionCheckpoint &cp = checkpoints[idx];

                        // project pos onto segment
                        godot::Vector3 p1    = cp.start_plane.project(in_point);
                        godot::Vector3 p2    = cp.end_plane.project(in_point);
                        float           cp_t = get_closest_t_on_segment(in_point, p1, p2);

                        // interpolate orientation
                        godot::Basis basis;
                        basis[0] = cp.orientation_start[0].lerp(cp.orientation_end[0], cp_t).normalized();
                        basis[2] = cp.orientation_start[2].lerp(cp.orientation_end[2], cp_t).normalized();
                        basis[1] = cp.orientation_start[1].lerp(cp.orientation_end[1], cp_t).normalized();

                        godot::Vector3 midpoint = cp.position_start.lerp(cp.position_end, cp_t);
                        godot::Plane    sep_x(basis[0], midpoint);
                        godot::Plane    sep_y(basis[1], midpoint);

                        float x_r = lerp(cp.x_radius_start_inv, cp.x_radius_end_inv, cp_t);
                        float y_r = lerp(cp.y_radius_start_inv, cp.y_radius_end_inv, cp_t);

                        float tx = sep_x.distance_to(in_point) * x_r;
                        float ty = sep_y.distance_to(in_point) * x_r;
                        float dist2 = tx * tx + ty * ty;

                        if (dist2 < best_dist2) {
                            best_dist2 = dist2;
                            best_cp    = idx;
                        }
                }
                return best_cp;
        }

        int get_best_checkpoint(godot::Vector3 in_p0, godot::Vector3 in_p1, godot::Vector3 sample_point)
        {
                std::vector<int> candidates;
                candidates.reserve(16);

                if (!checkpoint_bvh.nodes.empty()) {
                        checkpoint_bvh.query_segment(0, in_p0, in_p1, candidates);
                } else {
                        for (int i = 0; i < num_checkpoints; i++)
                                candidates.push_back(i);
                }

                candidates.erase(std::remove_if(candidates.begin(), candidates.end(), [&](int idx){
                        return !checkpoints[idx].start_plane.is_point_over(sample_point) || checkpoints[idx].end_plane.is_point_over(sample_point);
                }), candidates.end());
                int   best_cp     = -1;
                float best_dist2  = std::numeric_limits<float>::infinity();
                for (int idx : candidates) {
                        const CollisionCheckpoint &cp = checkpoints[idx];

                        // project pos onto segment
                        godot::Vector3 p1    = cp.start_plane.project(sample_point);
                        godot::Vector3 p2    = cp.end_plane.project(sample_point);
                        float           cp_t = get_closest_t_on_segment(sample_point, p1, p2);

                        godot::Basis basis;
                        basis[0] = cp.orientation_start[0].lerp(cp.orientation_end[0], cp_t).normalized();
                        basis[2] = cp.orientation_start[2].lerp(cp.orientation_end[2], cp_t).normalized();
                        basis[1] = cp.orientation_start[1].lerp(cp.orientation_end[1], cp_t).normalized();

                        godot::Vector3 midpoint = cp.position_start.lerp(cp.position_end, cp_t);
                        godot::Plane    sep_x(basis[0], midpoint);
                        godot::Plane    sep_y(basis[1], midpoint);

                        float x_r = lerp(cp.x_radius_start_inv, cp.x_radius_end_inv, cp_t);
                        float y_r = lerp(cp.y_radius_start_inv, cp.y_radius_end_inv, cp_t);

                        float tx = sep_x.distance_to(sample_point) * x_r;
                        float ty = sep_y.distance_to(sample_point) * x_r;
                        float dist2 = tx * tx + ty * ty;

                        if (dist2 < best_dist2) {
                            best_dist2 = dist2;
                            best_cp    = idx;
                        }
                }
                return best_cp;
        }
};