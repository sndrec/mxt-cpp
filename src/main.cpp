#include "main.h"
#include "godot_cpp/core/class_db.hpp"
#include "godot_cpp/classes/engine.hpp"
#include "godot_cpp/classes/input.hpp"
#include "godot_cpp/classes/viewport.hpp"
#include "godot_cpp/classes/world3d.hpp"
#include "godot_cpp/variant/utility_functions.hpp"
#include "godot_cpp/variant/string_name.hpp"
#include "godot_cpp/core/math.hpp"
#include "mxt_core/curve.h"
#include "mxt_core/enums.h"
#include "track/racetrack.h"
#include "track/trigger_collider.h"
#include "track/road_modulation.h"
#include "track/road_embed.h"
#include "car/physics_car.h"
#include "godot_cpp/variant/array.hpp"
#include "godot_cpp/variant/packed_byte_array.hpp"
#include "godot_cpp/variant/dictionary.hpp"
#include "godot_cpp/variant/string.hpp"
#include "godot_cpp/classes/stream_peer_buffer.hpp"
#include "mxt_core/math_utils.h"
#include <chrono>
#include <cfloat>
#include <cfenv>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <numeric>
#include <new>
#include <type_traits>
#include <vector>
#if defined(_MSC_VER)
#include <malloc.h>
#endif
#if defined(__SSE__)
#include <xmmintrin.h>
#endif
#include "mxt_core/debug.hpp"

using namespace godot;

struct MeshBVHBuildPrim
{
	SimAABB bounds;
	SimVec3 center;
	int32_t triangle_index;
};

static SimAABB mesh_bvh_merge_bounds(const SimAABB &a, const SimAABB &b)
{
	SimAABB out = a;
	out.expand_to(b.position);
	out.expand_to(b.position + b.size);
	return out;
}

static int mesh_bvh_longest_axis(const SimAABB &bounds)
{
	if (bounds.size.x >= bounds.size.y && bounds.size.x >= bounds.size.z) {
		return 0;
	}
	if (bounds.size.y >= bounds.size.z) {
		return 1;
	}
	return 2;
}

static float mesh_bvh_axis_value(const SimVec3 &value, int axis)
{
	if (axis == 0) {
		return value.x;
	}
	if (axis == 1) {
		return value.y;
	}
	return value.z;
}

static float mesh_bvh_surface_area(const SimAABB &bounds)
{
	const SimVec3 s = bounds.size;
	return 2.0f * (s.x * s.y + s.y * s.z + s.z * s.x);
}

struct MeshBVHBuildRef
{
	SimAABB bounds;
	int32_t child = -1;
	int32_t count = -1;
};

static TrackMeshBVHNode mesh_bvh_empty_node()
{
	TrackMeshBVHNode node = {};
	for (int i = 0; i < MXT_MESH_BVH_WIDTH; ++i) {
		node.min_x[i] = FLT_MAX;
		node.min_y[i] = FLT_MAX;
		node.min_z[i] = FLT_MAX;
		node.max_x[i] = -FLT_MAX;
		node.max_y[i] = -FLT_MAX;
		node.max_z[i] = -FLT_MAX;
		node.child[i] = -1;
		node.count[i] = -1;
	}
	return node;
}

static void mesh_bvh_set_child(TrackMeshBVHNode &node, int slot, const MeshBVHBuildRef &child)
{
	node.min_x[slot] = child.bounds.position.x;
	node.min_y[slot] = child.bounds.position.y;
	node.min_z[slot] = child.bounds.position.z;
	node.max_x[slot] = child.bounds.position.x + child.bounds.size.x;
	node.max_y[slot] = child.bounds.position.y + child.bounds.size.y;
	node.max_z[slot] = child.bounds.position.z + child.bounds.size.z;
	node.child[slot] = child.child;
	node.count[slot] = child.count;
}

struct MeshBVHBuildGroup
{
	int start = 0;
	int count = 0;
	SimAABB bounds;
	SimAABB centroid_bounds;
};

static void mesh_bvh_compute_group_bounds(std::vector<MeshBVHBuildPrim> &prims, MeshBVHBuildGroup &group)
{
	group.bounds = prims[group.start].bounds;
	group.centroid_bounds.position = prims[group.start].center;
	group.centroid_bounds.size = SimVec3();
	for (int i = group.start + 1; i < group.start + group.count; ++i) {
		group.bounds = mesh_bvh_merge_bounds(group.bounds, prims[i].bounds);
		group.centroid_bounds.expand_to(prims[i].center);
	}
}

static int mesh_bvh_partition_group(std::vector<MeshBVHBuildPrim> &prims, const MeshBVHBuildGroup &group)
{
	constexpr int kBinCount = 16;
	struct MeshBVHBin {
		SimAABB bounds;
		int count = 0;
	};
	int best_axis = -1;
	int best_bin = -1;
	float best_cost = FLT_MAX;
	for (int axis = 0; axis < 3; ++axis) {
		const float cmin = mesh_bvh_axis_value(group.centroid_bounds.position, axis);
		const float cmax = mesh_bvh_axis_value(group.centroid_bounds.position + group.centroid_bounds.size, axis);
		const float extent = cmax - cmin;
		if (extent <= 1.0e-6f) {
			continue;
		}
		MeshBVHBin bins[kBinCount];
		for (int i = group.start; i < group.start + group.count; ++i) {
			int bin_index = static_cast<int>(((mesh_bvh_axis_value(prims[i].center, axis) - cmin) / extent) * static_cast<float>(kBinCount));
			if (bin_index < 0) {
				bin_index = 0;
			} else if (bin_index >= kBinCount) {
				bin_index = kBinCount - 1;
			}
			MeshBVHBin &bin = bins[bin_index];
			if (bin.count == 0) {
				bin.bounds = prims[i].bounds;
			} else {
				bin.bounds = mesh_bvh_merge_bounds(bin.bounds, prims[i].bounds);
			}
			bin.count++;
		}
		SimAABB left_bounds[kBinCount - 1];
		SimAABB right_bounds[kBinCount - 1];
		int left_count[kBinCount - 1] = {};
		int right_count[kBinCount - 1] = {};
		bool left_valid = false;
		SimAABB left_accum;
		int left_accum_count = 0;
		for (int bin = 0; bin < kBinCount - 1; ++bin) {
			if (bins[bin].count > 0) {
				left_accum = left_valid ? mesh_bvh_merge_bounds(left_accum, bins[bin].bounds) : bins[bin].bounds;
				left_valid = true;
				left_accum_count += bins[bin].count;
			}
			left_bounds[bin] = left_accum;
			left_count[bin] = left_accum_count;
		}
		bool right_valid = false;
		SimAABB right_accum;
		int right_accum_count = 0;
		for (int bin = kBinCount - 1; bin > 0; --bin) {
			if (bins[bin].count > 0) {
				right_accum = right_valid ? mesh_bvh_merge_bounds(right_accum, bins[bin].bounds) : bins[bin].bounds;
				right_valid = true;
				right_accum_count += bins[bin].count;
			}
			right_bounds[bin - 1] = right_accum;
			right_count[bin - 1] = right_accum_count;
		}
		for (int bin = 0; bin < kBinCount - 1; ++bin) {
			if (left_count[bin] == 0 || right_count[bin] == 0) {
				continue;
			}
			const float cost =
				mesh_bvh_surface_area(left_bounds[bin]) * static_cast<float>(left_count[bin]) +
				mesh_bvh_surface_area(right_bounds[bin]) * static_cast<float>(right_count[bin]);
			if (cost < best_cost) {
				best_cost = cost;
				best_axis = axis;
				best_bin = bin;
			}
		}
	}
	int mid = group.start + group.count / 2;
	if (best_axis >= 0) {
		const float cmin = mesh_bvh_axis_value(group.centroid_bounds.position, best_axis);
		const float cmax = mesh_bvh_axis_value(group.centroid_bounds.position + group.centroid_bounds.size, best_axis);
		const float extent = cmax - cmin;
		const float split_pos = cmin + extent * (static_cast<float>(best_bin + 1) / static_cast<float>(kBinCount));
		auto mid_it = std::partition(
			prims.begin() + group.start,
			prims.begin() + group.start + group.count,
			[best_axis, split_pos](const MeshBVHBuildPrim &prim) {
				return mesh_bvh_axis_value(prim.center, best_axis) < split_pos;
			});
		mid = static_cast<int>(mid_it - prims.begin());
	}
	if (mid <= group.start || mid >= group.start + group.count) {
		const int axis = mesh_bvh_longest_axis(group.centroid_bounds);
		mid = group.start + group.count / 2;
		std::nth_element(
			prims.begin() + group.start,
			prims.begin() + mid,
			prims.begin() + group.start + group.count,
			[axis](const MeshBVHBuildPrim &a, const MeshBVHBuildPrim &b) {
				return mesh_bvh_axis_value(a.center, axis) < mesh_bvh_axis_value(b.center, axis);
			});
	}
	return mid;
}

static MeshBVHBuildRef build_mesh_bvh_child(std::vector<MeshBVHBuildPrim> &prims, std::vector<TrackMeshBVHNode> &nodes, int start, int count)
{
	constexpr int kLeafTriangleCount = 8;
	MeshBVHBuildGroup root_group;
	root_group.start = start;
	root_group.count = count;
	mesh_bvh_compute_group_bounds(prims, root_group);
	if (count <= kLeafTriangleCount) {
		MeshBVHBuildRef ref;
		ref.bounds = root_group.bounds;
		ref.child = start;
		ref.count = count;
		return ref;
	}

	const int node_index = static_cast<int>(nodes.size());
	nodes.push_back(mesh_bvh_empty_node());

	MeshBVHBuildGroup groups[MXT_MESH_BVH_WIDTH];
	int group_count = 1;
	groups[0] = root_group;
	while (group_count < MXT_MESH_BVH_WIDTH) {
		int split_group_index = -1;
		float split_score = -1.0f;
		for (int i = 0; i < group_count; ++i) {
			if (groups[i].count <= kLeafTriangleCount) {
				continue;
			}
			const float score = mesh_bvh_surface_area(groups[i].bounds) * static_cast<float>(groups[i].count);
			if (score > split_score) {
				split_score = score;
				split_group_index = i;
			}
		}
		if (split_group_index < 0) {
			break;
		}
		const MeshBVHBuildGroup split_group = groups[split_group_index];
		const int mid = mesh_bvh_partition_group(prims, split_group);
		if (mid <= split_group.start || mid >= split_group.start + split_group.count) {
			break;
		}
		MeshBVHBuildGroup left;
		left.start = split_group.start;
		left.count = mid - split_group.start;
		mesh_bvh_compute_group_bounds(prims, left);
		MeshBVHBuildGroup right;
		right.start = mid;
		right.count = split_group.start + split_group.count - mid;
		mesh_bvh_compute_group_bounds(prims, right);
		groups[split_group_index] = left;
		groups[group_count++] = right;
	}

	for (int i = 0; i < group_count; ++i) {
		MeshBVHBuildRef child_ref;
		if (groups[i].count <= kLeafTriangleCount) {
			child_ref.bounds = groups[i].bounds;
			child_ref.child = groups[i].start;
			child_ref.count = groups[i].count;
		} else {
			child_ref = build_mesh_bvh_child(prims, nodes, groups[i].start, groups[i].count);
		}
		mesh_bvh_set_child(nodes[node_index], i, child_ref);
	}

	MeshBVHBuildRef ref;
	ref.bounds = root_group.bounds;
	ref.child = node_index;
	ref.count = 0;
	return ref;
}

static void build_mesh_bvh(std::vector<MeshBVHBuildPrim> &prims, std::vector<TrackMeshBVHNode> &nodes)
{
	nodes.clear();
	if (prims.empty()) {
		return;
	}
	nodes.reserve(prims.size() * 2u);
	const MeshBVHBuildRef root = build_mesh_bvh_child(prims, nodes, 0, static_cast<int>(prims.size()));
	if (root.count > 0) {
		TrackMeshBVHNode root_node = mesh_bvh_empty_node();
		mesh_bvh_set_child(root_node, 0, root);
		nodes.push_back(root_node);
	}
}

static void build_track_mesh_world_bvh(RaceTrack *track, HeapHandler &level_data)
{
	if (track->num_mesh_collision_triangles <= 0) {
		return;
	}
	std::vector<MeshBVHBuildPrim> prims;
	prims.reserve(track->num_mesh_collision_triangles);
	for (int tri_index = 0; tri_index < track->num_mesh_collision_triangles; ++tri_index) {
		const TrackMeshCollisionTriangle &tri = track->mesh_collision_triangles[tri_index];
		if (terrain_mesh_is_overlay_surface(tri.terrain)) {
			continue;
		}
		MeshBVHBuildPrim prim;
		prim.bounds = tri.bounds;
		prim.center = (tri.p0 + tri.p1 + tri.p2) * (1.0f / 3.0f);
		prim.triangle_index = tri_index;
		prims.push_back(prim);
	}
	if (prims.empty()) {
		return;
	}
	std::vector<TrackMeshBVHNode> nodes;
	build_mesh_bvh(prims, nodes);
	track->num_mesh_world_bvh_nodes = static_cast<int>(nodes.size());
	track->mesh_world_bvh_nodes = level_data.allocate_array<TrackMeshBVHNode>(nodes.size());
	track->mesh_world_bvh_triangle_indices = level_data.allocate_array<int32_t>(prims.size());
	for (int i = 0; i < track->num_mesh_world_bvh_nodes; ++i) {
		track->mesh_world_bvh_nodes[i] = nodes[i];
	}
	for (int i = 0; i < static_cast<int>(prims.size()); ++i) {
		track->mesh_world_bvh_triangle_indices[i] = prims[i].triangle_index;
	}
}

static void build_track_mesh_floor_bvh(RaceTrack *track, HeapHandler &level_data)
{
	if (track->num_mesh_collision_triangles <= 0) {
		return;
	}
	std::vector<MeshBVHBuildPrim> prims;
	prims.reserve(track->num_mesh_collision_triangles);
	for (int tri_index = 0; tri_index < track->num_mesh_collision_triangles; ++tri_index) {
		const TrackMeshCollisionTriangle &tri = track->mesh_collision_triangles[tri_index];
		if (!terrain_mesh_has_floor_response(tri.terrain)) {
			continue;
		}
		MeshBVHBuildPrim prim;
		prim.bounds = tri.bounds;
		prim.center = (tri.p0 + tri.p1 + tri.p2) * (1.0f / 3.0f);
		prim.triangle_index = tri_index;
		prims.push_back(prim);
	}
	if (prims.empty()) {
		return;
	}
	std::vector<TrackMeshBVHNode> nodes;
	build_mesh_bvh(prims, nodes);
	track->num_mesh_floor_bvh_nodes = static_cast<int>(nodes.size());
	track->mesh_floor_bvh_nodes = level_data.allocate_array<TrackMeshBVHNode>(nodes.size());
	track->mesh_floor_bvh_triangle_indices = level_data.allocate_array<int32_t>(prims.size());
	for (int i = 0; i < track->num_mesh_floor_bvh_nodes; ++i) {
		track->mesh_floor_bvh_nodes[i] = nodes[i];
	}
	for (int i = 0; i < static_cast<int>(prims.size()); ++i) {
		track->mesh_floor_bvh_triangle_indices[i] = prims[i].triangle_index;
	}
}

static void build_track_mesh_overlay_bvh(RaceTrack *track, HeapHandler &level_data)
{
	if (track->num_mesh_collision_triangles <= 0) {
		return;
	}
	std::vector<MeshBVHBuildPrim> prims;
	prims.reserve(track->num_mesh_collision_triangles);
	for (int tri_index = 0; tri_index < track->num_mesh_collision_triangles; ++tri_index) {
		const TrackMeshCollisionTriangle &tri = track->mesh_collision_triangles[tri_index];
		if (!terrain_mesh_is_overlay_surface(tri.terrain)) {
			continue;
		}
		MeshBVHBuildPrim prim;
		prim.bounds = tri.bounds;
		prim.center = (tri.p0 + tri.p1 + tri.p2) * (1.0f / 3.0f);
		prim.triangle_index = tri_index;
		prims.push_back(prim);
	}
	if (prims.empty()) {
		return;
	}
	std::vector<TrackMeshBVHNode> nodes;
	build_mesh_bvh(prims, nodes);
	track->num_mesh_overlay_bvh_nodes = static_cast<int>(nodes.size());
	track->mesh_overlay_bvh_nodes = level_data.allocate_array<TrackMeshBVHNode>(nodes.size());
	track->mesh_overlay_bvh_triangle_indices = level_data.allocate_array<int32_t>(prims.size());
	for (int i = 0; i < track->num_mesh_overlay_bvh_nodes; ++i) {
		track->mesh_overlay_bvh_nodes[i] = nodes[i];
	}
	for (int i = 0; i < static_cast<int>(prims.size()); ++i) {
		track->mesh_overlay_bvh_triangle_indices[i] = prims[i].triangle_index;
	}
}

static SimVec3 normalize_mesh_source_normal_or_abort(const SimVec3 &normal, uint32_t tri_index, const char *name)
{
	const float len2 = normal.length_squared();
	if (len2 <= 1.0e-8f) {
		UtilityFunctions::printerr(String("MXT mesh collision triangle "), static_cast<int64_t>(tri_index), String(" has invalid "), name);
		std::abort();
	}
	return normal / sqrtf(len2);
}

static void precompute_mesh_triangle_projection(TrackMeshCollisionTriangle &tri, uint32_t tri_index)
{
	tri.edge0 = tri.p1 - tri.p0;
	tri.edge1 = tri.p2 - tri.p0;
	const SimVec3 face = tri.edge0.cross(tri.edge1);
	const float face_len2 = face.length_squared();
	if (face_len2 <= 1.0e-8f) {
		UtilityFunctions::printerr(String("MXT mesh collision triangle "), static_cast<int64_t>(tri_index), String(" is degenerate"));
		std::abort();
	}
	tri.face_normal = face / sqrtf(face_len2);
	tri.projection_d00 = tri.edge0.dot(tri.edge0);
	tri.projection_d01 = tri.edge0.dot(tri.edge1);
	tri.projection_d11 = tri.edge1.dot(tri.edge1);
	const float denom = tri.projection_d00 * tri.projection_d11 - tri.projection_d01 * tri.projection_d01;
	if (fabsf(denom) <= 1.0e-8f) {
		UtilityFunctions::printerr(String("MXT mesh collision triangle "), static_cast<int64_t>(tri_index), String(" has invalid projection domain"));
		std::abort();
	}
	tri.projection_inv_denom = 1.0f / denom;
	tri.n0 = normalize_mesh_source_normal_or_abort(tri.n0, tri_index, "n0");
	tri.n1 = normalize_mesh_source_normal_or_abort(tri.n1, tri_index, "n1");
	tri.n2 = normalize_mesh_source_normal_or_abort(tri.n2, tri_index, "n2");
}

#define LOAD_INDEXED_VEC3(storage, name, index) SimVec3((storage).name##_x[(index)], (storage).name##_y[(index)], (storage).name##_z[(index)])
#define STORE_INDEXED_VEC3(storage, name, index, value) do { const SimVec3 mxt_v3_tmp = (value); (storage).name##_x[(index)] = mxt_v3_tmp.x; (storage).name##_y[(index)] = mxt_v3_tmp.y; (storage).name##_z[(index)] = mxt_v3_tmp.z; } while (0)

namespace {
	static inline godot::Vector3 gd_vec3(const SimVec3& v)
	{
		return godot::Vector3(v.x, v.y, v.z);
	}

	static inline godot::Basis gd_basis(const SimBasis& b)
	{
		godot::Basis out;
		out.set_column(0, gd_vec3(b.c0));
		out.set_column(1, gd_vec3(b.c1));
		out.set_column(2, gd_vec3(b.c2));
		return out;
	}

	static inline godot::Transform3D gd_transform(const SimTransform& t)
	{
		return godot::Transform3D(gd_basis(t.basis), gd_vec3(t.origin));
	}

	static godot::Transform3D build_camera_transform(const godot::Vector3& position, const godot::Vector3& interest, const godot::Vector3& up)
	{
		godot::Vector3 backward = position - interest;
		if (backward.length_squared() <= 0.0000001f) {
			return godot::Transform3D(godot::Basis(), position);
		}
		backward.normalize();
		godot::Vector3 right = up.cross(backward);
		if (right.length_squared() <= 0.0000001f) {
			right = godot::Vector3(1.0f, 0.0f, 0.0f);
		} else {
			right.normalize();
		}
		godot::Vector3 corrected_up = backward.cross(right);
		if (corrected_up.length_squared() <= 0.0000001f) {
			corrected_up = godot::Vector3(0.0f, 1.0f, 0.0f);
		} else {
			corrected_up.normalize();
		}
		godot::Basis basis;
		basis.set_column(0, right);
		basis.set_column(1, corrected_up);
		basis.set_column(2, backward);
		return godot::Transform3D(basis, position);
	}

	static float smoothstep01(float alpha)
	{
		alpha = std::max(0.0f, std::min(1.0f, alpha));
		return alpha * alpha * (3.0f - 2.0f * alpha);
	}

	static uint32_t bumper_hash_u32(uint32_t x)
	{
		x ^= x >> 16;
		x *= 0x7feb352du;
		x ^= x >> 15;
		x *= 0x846ca68bu;
		x ^= x >> 16;
		return x;
	}

	static uint32_t bumper_mix_u32(uint32_t state, uint32_t value)
	{
		return bumper_hash_u32(state ^ (value + 0x9E3779B9u + (state << 6) + (state >> 2)));
	}

	static uint32_t bumper_float_bits(float value)
	{
		uint32_t bits = 0;
		std::memcpy(&bits, &value, sizeof(bits));
		return bits;
	}

	static uint32_t bumper_track_seed_from_track(const RaceTrack* track)
	{
		if (!track) {
			return 0xB62A1C3Du;
		}
		uint32_t seed = 0xB62A1C3Du;
		seed = bumper_mix_u32(seed, static_cast<uint32_t>(track->num_checkpoints));
		seed = bumper_mix_u32(seed, static_cast<uint32_t>(track->num_segments));
		seed = bumper_mix_u32(seed, bumper_float_bits(track->lap_length));
		for (int i = 0; i < track->num_checkpoints; ++i) {
			const CollisionCheckpoint& cp = track->checkpoints[i];
			seed = bumper_mix_u32(seed, bumper_float_bits(cp.position_start.x));
			seed = bumper_mix_u32(seed, bumper_float_bits(cp.position_start.y));
			seed = bumper_mix_u32(seed, bumper_float_bits(cp.position_start.z));
			seed = bumper_mix_u32(seed, bumper_float_bits(cp.position_end.x));
			seed = bumper_mix_u32(seed, bumper_float_bits(cp.position_end.y));
			seed = bumper_mix_u32(seed, bumper_float_bits(cp.position_end.z));
			seed = bumper_mix_u32(seed, bumper_float_bits(cp.distance));
			seed = bumper_mix_u32(seed, bumper_float_bits(cp.local_distance));
		}
		return seed ? seed : 0xB62A1C3Du;
	}

	static float bumper_sequence_trigger_distance(uint32_t spawn_seed, int leader_lap, uint32_t sequence, float interval, float lap_length)
	{
		const uint32_t hash = bumper_hash_u32(
			static_cast<uint32_t>(spawn_seed) ^
			(static_cast<uint32_t>(leader_lap) * 0x27D4EB2Du) ^
			(sequence * 0x9E3779B9u) ^
			0xA341316Cu);
		const float jitter = (static_cast<float>(hash & 0xffffu) * (1.0f / 65535.0f) - 0.5f) * interval * 0.35f;
		const float first_trigger = leader_lap == 2 ? 680.0f : 360.0f;
		(void)lap_length;
		return first_trigger + static_cast<float>(sequence) * interval + jitter;
	}

	static void populate_visual_car_args(godot::Array& visual_args, const PhysicsCar& car)
	{
		visual_args[0] = gd_vec3(LOAD_INDEXED_VEC3(*car.soa, position_current, car.soa_index));
		visual_args[1] = gd_vec3(LOAD_INDEXED_VEC3(*car.soa, position_old, car.soa_index));
		visual_args[2] = gd_vec3(LOAD_INDEXED_VEC3(*car.soa, track_surface_normal, car.soa_index));
		visual_args[3] = car.soa->height_above_track[car.soa_index];
		visual_args[4] = gd_vec3(LOAD_INDEXED_VEC3(*car.soa, velocity, car.soa_index));
		visual_args[5] = gd_vec3(LOAD_INDEXED_VEC3(*car.soa, velocity_angular, car.soa_index));
		visual_args[6] = gd_transform(MXT_LOAD_TRANSFORM(*car.soa, basis_physical, car.soa_index));
		visual_args[7] = car.soa->base_speed[car.soa_index];
		visual_args[8] = car.soa->boost_turbo[car.soa_index];
		visual_args[9] = car.soa->speed_kmh[car.soa_index];
		visual_args[10] = car.soa->energy[car.soa_index];
		visual_args[11] = car.soa->lap_progress[car.soa_index];
		visual_args[12] = car.soa->boost_frames[car.soa_index];
		visual_args[13] = car.soa->boost_frames_manual[car.soa_index];
		visual_args[14] = car.soa->lap[car.soa_index];
		visual_args[15] = car.soa->machine_state[car.soa_index];
		visual_args[16] = car.soa->terrain_state[car.soa_index];
		visual_args[17] = car.soa->frames_since_start_2[car.soa_index];
		visual_args[18] = car.soa->tilt_state[car.soa_index * 4];
		visual_args[19] = car.soa->input_strafe[car.soa_index];
		visual_args[20] = car.soa->turn_reaction_input[car.soa_index];
		visual_args[21] = car.soa->g_anim_timer[car.soa_index];
		visual_args[22] = car.soa->state_2[car.soa_index];
		visual_args[23] = gd_vec3(SimVec3(car.soa->tilt_offset_x[car.soa_index * 4], car.soa->tilt_offset_y[car.soa_index * 4], car.soa->tilt_offset_z[car.soa_index * 4]));
		visual_args[24] = gd_vec3(SimVec3(car.soa->tilt_offset_x[car.soa_index * 4 + 2], car.soa->tilt_offset_y[car.soa_index * 4 + 2], car.soa->tilt_offset_z[car.soa_index * 4 + 2]));
		visual_args[25] = car.soa->stat_weight[car.soa_index];
		visual_args[26] = car.soa->stat_strafe[car.soa_index];
		visual_args[27] = car.soa->input_strafe_1_6[car.soa_index];
		visual_args[28] = car.soa->weight_derived_1[car.soa_index];
		visual_args[29] = car.soa->weight_derived_2[car.soa_index];
		visual_args[30] = car.soa->weight_derived_3[car.soa_index];
		visual_args[31] = gd_vec3(LOAD_INDEXED_VEC3(*car.soa, visual_rotation, car.soa_index));
		visual_args[32] = car.soa->spinattack_angle[car.soa_index];
		visual_args[33] = car.soa->spinattack_direction[car.soa_index];
		visual_args[34] = car.soa->visual_shake_mult[car.soa_index];
		visual_args[35] = car.soa->input_accel[car.soa_index];
		visual_args[36] = car.soa->restore_state[car.soa_index];
		visual_args[37] = car.soa->restore_move_frames[car.soa_index];
		visual_args[38] = gd_transform(MXT_LOAD_TRANSFORM(*car.soa, restore_start_transform, car.soa_index));
		visual_args[39] = gd_transform(MXT_LOAD_TRANSFORM(*car.soa, restore_target_transform, car.soa_index));
		visual_args[40] = static_cast<int>(car.get_s_boost_charge());
		visual_args[41] = static_cast<int>(car.get_s_boost_max_charge());
		visual_args[42] = car.is_s_boost_active();
		visual_args[43] = car.is_s_boost_ready();
		visual_args[44] = car.soa->tilt_state[car.soa_index * 4 + 1];
		visual_args[45] = car.soa->tilt_state[car.soa_index * 4 + 2];
		visual_args[46] = car.soa->tilt_state[car.soa_index * 4 + 3];
		visual_args[47] = car.soa->camera_reorienting[car.soa_index];
		visual_args[48] = car.soa->camera_repositioning[car.soa_index];
		visual_args[49] = gd_vec3(LOAD_INDEXED_VEC3(*car.soa, track_surface_pos, car.soa_index));
		visual_args[50] = car.soa->calced_max_energy[car.soa_index];
		visual_args[51] = static_cast<int>(car.soa->attack_cooldown_frames[car.soa_index]);
	}

	static inline float fzgx_angle_units_to_rad(float units)
	{
		return units * (TAU / 65536.0f);
	}

	static inline float fzgx_sin_u16(uint32_t units)
	{
		return sinf(fzgx_angle_units_to_rad(static_cast<float>(static_cast<uint16_t>(units))));
	}

	static inline uint32_t float_bits_exact(float value)
	{
		uint32_t bits = 0;
		std::memcpy(&bits, &value, sizeof(bits));
		return bits;
	}

	static inline float safe_visual_div(float numerator, float denominator)
	{
		return std::abs(denominator) > 0.0001f ? numerator / denominator : 0.0f;
	}

	static inline void rotate_about_x_right(SimTransform& transform, float angle_units)
	{
		transform.basis = transform.basis * SimBasis(SimQuat(SimVec3(1.0f, 0.0f, 0.0f), fzgx_angle_units_to_rad(angle_units)));
	}

	static inline void rotate_about_y_right(SimTransform& transform, float angle_units)
	{
		transform.basis = transform.basis * SimBasis(SimQuat(SimVec3(0.0f, 1.0f, 0.0f), fzgx_angle_units_to_rad(angle_units)));
	}

	static inline void rotate_about_z_right(SimTransform& transform, float angle_units)
	{
		transform.basis = transform.basis * SimBasis(SimQuat(SimVec3(0.0f, 0.0f, 1.0f), fzgx_angle_units_to_rad(angle_units)));
	}

	static SimTransform compose_machine_visual_transform_for_render(PhysicsCarSoA& c, int i, GameSim::RenderVehicleVisualState& render_state, bool advance_state, bool store_side_effects)
	{
		SimTransform current_transform = MXT_LOAD_TRANSFORM(c, basis_physical, i);
		const SimVec3 position = LOAD_INDEXED_VEC3(c, position_current, i);
		current_transform.origin = position;

		float startup_wobble = 0.0f;
		if (c.base_speed[i] <= 2.0f) {
			startup_wobble = (2.0f - c.base_speed[i]) * 0.5f;
		}
		if (c.frames_since_start_2[i] < 90u) {
			startup_wobble *= static_cast<float>(c.frames_since_start_2[i]) / 90.0f;
		}
		float use_startup_wobble = render_state.startup_wobble;
		if (advance_state) {
			render_state.startup_wobble += 0.05f * (startup_wobble - render_state.startup_wobble);
			use_startup_wobble = render_state.startup_wobble;
		}
		const float startup_roll_offset = static_cast<float>(static_cast<int16_t>(static_cast<int>(
			182.04445f * 0.5f * (use_startup_wobble * fzgx_sin_u16(c.g_anim_timer[i] * 0x109u)))));

		float vertical_offset = 0.006f * (use_startup_wobble * fzgx_sin_u16(c.g_anim_timer[i] * 0x1a3u));
		const SimVec3 visual_origin = position + current_transform.basis.xform(
			SimVec3(0.0f, vertical_offset - 0.2f * use_startup_wobble, 0.0f));

		current_transform.orthonormalize();
		current_transform.origin = SimVec3();

		{
			const int point_base = i * 4;
			const float front_z = c.tilt_offset_z[point_base + 0];
			const float back_z = c.tilt_offset_z[point_base + 2];
			float suspension_pitch = 0.0f;
			if (std::abs(front_z) > 0.0001f) {
				suspension_pitch = back_z / -front_z - 1.0f;
			}
			suspension_pitch = std::max(-0.2f, std::min(0.2f, suspension_pitch));
			SimTransform pitch_transform = current_transform;
			rotate_about_x_right(pitch_transform, static_cast<float>(static_cast<int>(182.04445f * 30.0f * suspension_pitch)));
			if (store_side_effects) {
				MXT_STORE_TRANSFORM(c, g_pitch_mtx_0x5e0, i, pitch_transform);
			}
		}

		const SimVec3 broken_down_angle = LOAD_INDEXED_VEC3(c, unk_vec3_0x4e4, i);
		rotate_about_z_right(current_transform, static_cast<float>(static_cast<int>(10430.378f * safe_visual_div(broken_down_angle.z, c.weight_derived_3[i]))));
		rotate_about_y_right(current_transform, static_cast<float>(static_cast<int>(10430.378f * safe_visual_div(broken_down_angle.y, c.weight_derived_2[i]))));
		rotate_about_x_right(current_transform, static_cast<float>(static_cast<int>(10430.378f * safe_visual_div(broken_down_angle.x, c.weight_derived_1[i]))));

		if ((c.state_2[i] & 0x20u) == 0u) {
			SimTransform local_visual;
			if ((c.machine_state[i] & MACHINESTATE::ACTIVE) != 0u) {
				float use_turn_reaction = render_state.turn_reaction_effect;
				if (advance_state) {
					render_state.turn_reaction_effect += 0.05f * (c.turn_reaction_input[i] - render_state.turn_reaction_effect);
					use_turn_reaction = render_state.turn_reaction_effect;
				}
				rotate_about_y_right(local_visual, static_cast<float>(static_cast<int>(182.04445f * use_turn_reaction)));
			}

			const SimVec3 velocity = LOAD_INDEXED_VEC3(c, velocity, i);
			const float speed_mag = velocity.length();
			const float speed_norm = safe_visual_div(speed_mag, c.stat_weight[i]) / 4.629629629f;
			const int16_t angular_roll_angle = static_cast<int16_t>(static_cast<int>(
				10430.378f * speed_norm * 4.5f * safe_visual_div(c.velocity_angular_y[i], c.weight_derived_2[i])));
			const int strafe_visual_roll = static_cast<int>(static_cast<int16_t>(static_cast<int>(
				182.04445f * (c.stat_strafe[i] / 15.0f) * -5.0f * c.input_strafe_1_6[i] * speed_norm)));
			if (advance_state) {
				render_state.strafe_visual_roll = strafe_visual_roll;
			}
			int combined_roll = static_cast<int>(angular_roll_angle) + strafe_visual_roll;

			float visual_pitch_effect = 1.0f - static_cast<float>(std::abs(combined_roll)) / 3640.0f;
			visual_pitch_effect = std::max(visual_pitch_effect, 0.0f);
			visual_pitch_effect *= 0.7f * safe_visual_div(c.visual_rotation_x[i], c.weight_derived_1[i]);
			visual_pitch_effect = std::max(-0.3f, std::min(0.3f, visual_pitch_effect));
			float visual_roll_effect = 2.5f * safe_visual_div(c.visual_rotation_z[i], c.weight_derived_3[i]);
			visual_roll_effect = std::max(-0.5f, std::min(0.5f, visual_roll_effect));

			rotate_about_x_right(local_visual, static_cast<float>(static_cast<int>(10430.378f * visual_pitch_effect)));
			combined_roll += static_cast<int>(static_cast<int16_t>(static_cast<int>(10430.378f * -visual_roll_effect)));
			combined_roll = std::max(-0x238e, std::min(0x238e, combined_roll));
			rotate_about_z_right(local_visual, static_cast<float>(static_cast<int>(static_cast<float>(static_cast<int16_t>(combined_roll)) + startup_roll_offset)));

			SimQuat target_quat = local_visual.basis.get_rotation_quaternion();
			SimQuat use_visual_quat = render_state.visual_quat;
			if (advance_state) {
				render_state.visual_quat = render_state.visual_quat.slerp(target_quat, 0.2f);
				use_visual_quat = render_state.visual_quat;
			}
			local_visual.basis = SimBasis(use_visual_quat);
			current_transform = current_transform * local_visual;

			if (c.spinattack_angle[i] != 0.0f) {
				const float spin_units = c.spinattack_angle[i] * (65536.0f / TAU);
				rotate_about_y_right(current_transform, c.spinattack_direction[i] == 0 ? spin_units : -spin_units);
			}
		} else {
			current_transform = MXT_LOAD_TRANSFORM(c, transform_visual, i);
		}

		current_transform.origin = visual_origin;

		const SimVec3 velocity = LOAD_INDEXED_VEC3(c, velocity, i);
		const SimVec3 angular_velocity = LOAD_INDEXED_VEC3(c, velocity_angular, i);
		const uint32_t velocity_hash = float_bits_exact(velocity.z) ^ float_bits_exact(velocity.x) ^ float_bits_exact(velocity.y);
		const float shake_scale = 0.00006f * c.visual_shake_mult[i];
		rotate_about_z_right(current_transform, static_cast<float>(static_cast<int>(
			10430.378f * shake_scale * (static_cast<float>((velocity_hash ^ float_bits_exact(angular_velocity.y)) & 0xffffu) / 65536.0f))));
		rotate_about_x_right(current_transform, static_cast<float>(static_cast<int>(
			10430.378f * shake_scale * (static_cast<float>((velocity_hash ^ float_bits_exact(angular_velocity.x)) & 0xffffu) / 65536.0f))));

		float use_height_adjust = render_state.height_adjust_from_boost;
		if (advance_state) {
			if ((c.machine_state[i] & MACHINESTATE::BOOSTING) == 0u) {
				render_state.height_adjust_from_boost -= 0.05f * render_state.height_adjust_from_boost;
			} else {
				const float pitch_adjust = std::max(0.0f, c.visual_rotation_x[i]);
				render_state.height_adjust_from_boost += 0.2f * (4.5f * safe_visual_div(pitch_adjust, c.weight_derived_1[i]) - render_state.height_adjust_from_boost);
				render_state.height_adjust_from_boost = std::min(render_state.height_adjust_from_boost, 0.3f);
			}
			use_height_adjust = render_state.height_adjust_from_boost;
		}
		current_transform.origin += current_transform.basis.get_column(1) * use_height_adjust;

		if ((c.terrain_state[i] & TERRAIN::DIRT) != 0u) {
			float dirt_scale = 0.1f + c.speed_kmh[i] / 900.0f;
			dirt_scale = std::min(dirt_scale, 1.0f);
			SimVec3 dirt_jitter(
				static_cast<float>((velocity_hash ^ float_bits_exact(angular_velocity.y)) & 0xffffu) / 65536.0f - 0.5f,
				0.0f,
				static_cast<float>((velocity_hash ^ float_bits_exact(angular_velocity.z)) & 0xffffu) / 65536.0f - 0.5f);
			dirt_jitter = current_transform.basis.xform(dirt_jitter) * (0.15f * dirt_scale);
			current_transform.origin += dirt_jitter;
		}

		if (store_side_effects) {
			MXT_STORE_TRANSFORM(c, transform_visual, i, current_transform);
		}
		return current_transform;
	}

	static void update_machine_visual_transform_for_render(PhysicsCarSoA& c, int i, GameSim::RenderVehicleVisualState& render_state)
	{
		compose_machine_visual_transform_for_render(c, i, render_state, true, true);
	}

	static SimTransform interpolate_sim_transform(const SimTransform& a, const SimTransform& b, float alpha)
	{
		alpha = std::max(0.0f, std::min(1.0f, alpha));
		SimTransform out;
		out.origin = a.origin.lerp(b.origin, alpha);
		const SimQuat qa = a.basis.get_rotation_quaternion();
		const SimQuat qb = b.basis.get_rotation_quaternion();
		out.basis = SimBasis(qa.slerp(qb, alpha));
		return out;
	}

	static bool render_correction_is_small(const SimTransform& correction)
	{
		const SimTransform identity;
		const float pos_error = correction.origin.length_squared();
		const float basis_error =
			(correction.basis.c0 - identity.basis.c0).length_squared() +
			(correction.basis.c1 - identity.basis.c1).length_squared() +
			(correction.basis.c2 - identity.basis.c2).length_squared();
		return pos_error < 0.000025f && basis_error < 0.000025f;
	}

	static SimTransform corrected_render_transform(const std::vector<SimTransform>& corrections,
			const std::vector<uint8_t>& active,
			int index,
			const SimTransform& transform)
	{
		if (index >= 0 &&
				index < static_cast<int>(active.size()) &&
				active[index] &&
				index < static_cast<int>(corrections.size())) {
			SimTransform out = transform;
			out.basis = out.basis * corrections[index].basis;
			out.origin += corrections[index].origin;
			return out;
		}
		return transform;
	}

	static SimTransform apply_render_correction(const SimTransform& transform, const SimTransform& correction)
	{
		SimTransform out = transform;
		out.basis = out.basis * correction.basis;
		out.origin += correction.origin;
		return out;
	}

	static inline godot::AABB gd_aabb(const SimAABB& b)
	{
		return godot::AABB(gd_vec3(b.position), gd_vec3(b.size));
	}

	static void* alloc_cache_aligned(size_t size)
	{
#if defined(_MSC_VER)
		return _aligned_malloc(size, 64);
#else
		void* ptr = nullptr;
		if (posix_memalign(&ptr, 64, size) != 0) {
			return nullptr;
		}
		return ptr;
#endif
	}

	static void free_cache_aligned(void* ptr)
	{
#if defined(_MSC_VER)
		_aligned_free(ptr);
#else
		::free(ptr);
#endif
	}

	static inline SimVec3 sim_vec3(const godot::Vector3& v)
	{
		return SimVec3(v.x, v.y, v.z);
	}

	static inline SimBasis sim_basis(const godot::Basis& b)
	{
		const godot::Vector3 c0 = b.get_column(0);
		const godot::Vector3 c1 = b.get_column(1);
		const godot::Vector3 c2 = b.get_column(2);
		return SimBasis(c0.x, c0.y, c0.z, c1.x, c1.y, c1.z, c2.x, c2.y, c2.z);
	}

	static inline SimTransform sim_transform(const godot::Transform3D& t)
	{
		return SimTransform(sim_basis(t.basis), sim_vec3(t.origin));
	}

	static bool invert_basis(const SimBasis& basis, SimBasis& out)
	{
		const SimVec3 a = basis.c0;
		const SimVec3 b = basis.c1;
		const SimVec3 c = basis.c2;
		const SimVec3 row0 = b.cross(c);
		const float det = a.dot(row0);
		if (std::abs(det) <= 1.0e-8f) {
			out = basis.transposed();
			return false;
		}

		const float inv_det = 1.0f / det;
		const SimVec3 r0 = row0 * inv_det;
		const SimVec3 r1 = c.cross(a) * inv_det;
		const SimVec3 r2 = a.cross(b) * inv_det;
		out.c0 = SimVec3(r0.x, r1.x, r2.x);
		out.c1 = SimVec3(r0.y, r1.y, r2.y);
		out.c2 = SimVec3(r0.z, r1.z, r2.z);
		return true;
	}

	static SimTransform invert_scaled_transform(const SimTransform& transform)
	{
		SimTransform out;
		if (!invert_basis(transform.basis, out.basis)) {
			return transform.affine_inverse();
		}
		out.origin = out.basis.xform(-transform.origin);
		return out;
	}

	struct DipSwitchDefinition {
		const char* key;
		const char* label;
		int flag;
	};

	const DipSwitchDefinition DIP_SWITCH_DEFINITIONS[] = {
		{"DIP_DRAW_RAYCASTS", "Draw Raycasts", DIP_SWITCH::DIP_DRAW_RAYCASTS},
		{"DIP_DRAW_CHECKPOINTS", "Draw Checkpoints", DIP_SWITCH::DIP_DRAW_CHECKPOINTS},
		{"DIP_DRAW_SEGMENT_SURF", "Draw Segment Surface", DIP_SWITCH::DIP_DRAW_SEGMENT_SURF},
		{"DIP_DRAW_TILT_CORNER_DATA", "Draw Tilt Corner Data", DIP_SWITCH::DIP_DRAW_TILT_CORNER_DATA},
		{"DIP_DRAW_SEG_BOUNDS", "Draw Segment Bounds", DIP_SWITCH::DIP_DRAW_SEG_BOUNDS},
		{"DIP_DRAW_BRANCH_CENTERLINE", "Draw Branch Centerline", DIP_SWITCH::DIP_DRAW_BRANCH_CENTERLINE},
		{"DIP_TRACE_RAIL_SAMPLING", "Trace Rail Sampling", DIP_SWITCH::DIP_TRACE_RAIL_SAMPLING},
		{"DIP_DRAW_RAIL_CANDIDATES", "Draw Rail Candidates", DIP_SWITCH::DIP_DRAW_RAIL_CANDIDATES},
		{"DIP_TRACE_PIPE_FLOOR", "Trace Pipe Floor", DIP_SWITCH::DIP_TRACE_PIPE_FLOOR},
		{"DIP_DRAW_MESH_FLOOR_TESTS", "Draw Mesh Floor Tests", DIP_SWITCH::DIP_DRAW_MESH_FLOOR_TESTS},
		{"DIP_DRAW_MESH_CAST_TESTS", "Draw Mesh Cast Tests", DIP_SWITCH::DIP_DRAW_MESH_CAST_TESTS},
		{"DIP_DRAW_MESH_COLLISION_HITS", "Draw Mesh Collision Hits", DIP_SWITCH::DIP_DRAW_MESH_COLLISION_HITS},
		{"DIP_TRACE_MESH_FLOOR", "Trace Mesh Floor", DIP_SWITCH::DIP_TRACE_MESH_FLOOR},
	};

	static bool vehicle_restore_off_eliminated(const PhysicsCarSoA& c, int i)
	{
		const uint32_t state = c.machine_state[i];
		if ((state & MACHINESTATE::COMPLETEDRACE_1_Q) != 0u) {
			return false;
		}
		if ((state & MACHINESTATE::FALLOUT) != 0u) {
			return true;
		}
		if (c.current_track[i] && c.position_current_y[i] < c.current_track[i]->minimum_y) {
			return true;
		}
		if ((state & MACHINESTATE::ZEROHP) == 0u) {
			return false;
		}
		if ((state & MACHINESTATE::RETIRED) != 0u) {
			return true;
		}
		return (c.state_2[i] & 0x80u) != 0u && (state & MACHINESTATE::AIRBORNE) == 0u;
	}

	static void begin_vehicle_tick_soa(PhysicsCarSoA& c, PhysicsCar* car_views, PlayerInput* inputs, uint32_t tick_count, int count, bool vehicle_restore_enabled, bool s_boost_enabled)
	{
		for (int i = 0; i < count; ++i) {
			PlayerInput& input = inputs[i];
			const float accel_raw = input.accelerate;
			c.simulation_tick[i] = tick_count;

			if (!s_boost_enabled) {
				c.s_boost_charge[i] = 0;
				c.s_boost_active[i] = false;
				c.s_boost_frames_remaining[i] = 0;
				c.s_boost_emit_frame_accumulator[i] = 0;
				c.s_boost_pending_spark_spawns[i] = 0;
				c.pending_super_sparks[i] = 0;
			} else if (!c.s_boost_active[i]) {
				c.s_boost_frames_remaining[i] = 0;
				c.s_boost_emit_frame_accumulator[i] = 0;
				c.s_boost_pending_spark_spawns[i] = 0;
			} else {
				if (c.s_boost_frames_remaining[i] > 0)
					c.s_boost_frames_remaining[i] -= 1;

				c.machine_state[i] &= ~(MACHINESTATE::TOOKDAMAGE | MACHINESTATE::LOWGRIP);

				c.s_boost_emit_frame_accumulator[i] += 1;
				while (c.s_boost_emit_frame_accumulator[i] >= 30) {
					c.s_boost_emit_frame_accumulator[i] -= 30;
					if (c.s_boost_pending_spark_spawns[i] < 255)
						c.s_boost_pending_spark_spawns[i] += 1;
				}

				if (c.s_boost_frames_remaining[i] == 0) {
					c.s_boost_active[i] = false;
					c.s_boost_emit_frame_accumulator[i] = 0;
					c.s_boost_pending_spark_spawns[i] = 0;
				}
			}

			const bool completed_race = (c.machine_state[i] & MACHINESTATE::COMPLETEDRACE_1_Q) != 0;
			const bool fell_out = c.current_track[i] && c.position_current_y[i] < c.current_track[i]->minimum_y;
			const bool zero_hp = c.energy[i] <= 0.0f;
			const bool restore_allowed = vehicle_restore_enabled || completed_race;
			if (!restore_allowed && (fell_out || zero_hp)) {
				if (fell_out) {
					c.machine_state[i] |= MACHINESTATE::FALLOUT;
				}
				if (zero_hp) {
					c.machine_state[i] |= MACHINESTATE::ZEROHP;
					c.energy[i] = 0.0f;
				}
				c.s_boost_active[i] = false;
				c.s_boost_frames_remaining[i] = 0;
				c.s_boost_emit_frame_accumulator[i] = 0;
				c.s_boost_pending_spark_spawns[i] = 0;
			}
			const bool needs_restore =
				restore_allowed &&
				c.current_track[i] &&
				(c.restore_state[i] != 0 || fell_out || zero_hp);
			if (needs_restore) {
				car_views[i].update_restore(accel_raw);
			}

			c.calced_max_energy[i] = c.car_properties[i]->max_energy + c.ko_energy_bonus[i];
			const bool in_startup_countdown = tick_count < c.level_start_time[i];
			if (!in_startup_countdown) {
				STORE_INDEXED_VEC3(c, initial_pos, i, LOAD_INDEXED_VEC3(c, position_current, i));
			}
			c.side_attack_indicator[i] = 0.0f;

			if (tick_count < c.level_start_time[i] - 180) {
				c.machine_state[i] |= MACHINESTATE::STARTINGCOUNTDOWN;
				c.machine_state[i] &= ~MACHINESTATE::ACTIVE;
			} else if (tick_count < c.level_start_time[i]) {
				c.machine_state[i] |= MACHINESTATE::STARTINGCOUNTDOWN;
				if (c.input_accel[i] > 0.01f)
					c.machine_state[i] |= MACHINESTATE::ACTIVE;
			} else {
				c.machine_state[i] &= ~MACHINESTATE::STARTINGCOUNTDOWN;
			}

			if ((c.machine_state[i] & MACHINESTATE::ZEROHP) ||
				(!vehicle_restore_enabled && (c.machine_state[i] & MACHINESTATE::FALLOUT))) {
				input.steer_horizontal = 0.0f;
				input.steer_vertical = 0.0f;
				input.boost = false;
				input.brake = 0.0f;
				input.strafe_left = 0.0f;
				input.strafe_right = 0.0f;
			}

			if (input.sideattack)
				c.machine_state[i] |= MACHINESTATE::SIDEATTACKING;
			if (input.spinattack)
				c.machine_state[i] |= MACHINESTATE::SPINATTACKING;
			if (input.boost && c.lap[i] > 1 && !c.s_boost_active[i])
				c.machine_state[i] |= MACHINESTATE::JUST_PRESSED_BOOST;

			c.g_anim_timer[i] += 1;
			STORE_INDEXED_VEC3(c, track_surface_normal_prev, i, LOAD_INDEXED_VEC3(c, track_surface_normal, i));
		}
	}

	static void finish_vehicle_tick_soa(PhysicsCarSoA& c, int count)
	{
		for (int i = 0; i < count; ++i) {
			SimTransform basis = MXT_LOAD_TRANSFORM(c, basis_physical, i);
			const SimVec3 pos(c.position_current_x[i], c.position_current_y[i], c.position_current_z[i]);
			const SimVec3 behind = basis.basis.xform(SimVec3(0.0f, 0.5f, 0.5f)) + pos;
			c.position_behind_x[i] = behind.x;
			c.position_behind_y[i] = behind.y;
			c.position_behind_z[i] = behind.z;
		}
	}

	static void update_damage_visual_geometry_soa(PhysicsCarSoA& c, int count);
	static void project_startup_velocity_and_speed_soa(PhysicsCarSoA& c, int count);

	static inline SimVec3 normalized_or_zero(const SimVec3& v)
	{
		return v.length_squared() > 0.000001f ? v.normalized() : SimVec3();
	}

	static inline SimVec3 remove_axis_component(const SimVec3& v, const SimVec3& axis)
	{
		return v - axis * v.dot(axis);
	}

	static inline SimVec3 keep_axis_component(const SimVec3& v, const SimVec3& axis)
	{
		return axis * v.dot(axis);
	}

	static void translate_contact_points_soa(PhysicsCarSoA& c, int i, const SimVec3& delta)
	{
		if (delta.length_squared() <= 0.0000001f) {
			return;
		}
		const int p = i * 4;
		const SimFloat4 dx(delta.x);
		const SimFloat4 dy(delta.y);
		const SimFloat4 dz(delta.z);
		sim_store4(c.tilt_pos_old_x + p, sim_load4(c.tilt_pos_old_x + p) + dx);
		sim_store4(c.tilt_pos_old_y + p, sim_load4(c.tilt_pos_old_y + p) + dy);
		sim_store4(c.tilt_pos_old_z + p, sim_load4(c.tilt_pos_old_z + p) + dz);
		sim_store4(c.tilt_pos_x + p, sim_load4(c.tilt_pos_x + p) + dx);
		sim_store4(c.tilt_pos_y + p, sim_load4(c.tilt_pos_y + p) + dy);
		sim_store4(c.tilt_pos_z + p, sim_load4(c.tilt_pos_z + p) + dz);
		sim_store4(c.wall_pos_a_x + p, sim_load4(c.wall_pos_a_x + p) + dx);
		sim_store4(c.wall_pos_a_y + p, sim_load4(c.wall_pos_a_y + p) + dy);
		sim_store4(c.wall_pos_a_z + p, sim_load4(c.wall_pos_a_z + p) + dz);
		sim_store4(c.wall_pos_b_x + p, sim_load4(c.wall_pos_b_x + p) + dx);
		sim_store4(c.wall_pos_b_y + p, sim_load4(c.wall_pos_b_y + p) + dy);
		sim_store4(c.wall_pos_b_z + p, sim_load4(c.wall_pos_b_z + p) + dz);
	}

	static void post_vehicle_tick_soa(PhysicsCarSoA& c, PhysicsCar* car_views, uint8_t* pending_s_boost_sparks, int count,
		bool s_boost_enabled,
		TrackQueryScratch &scratch)
	{
		for (int i = 0; i < count; ++i) {
			if ((c.state_2[i] & 0x8u) && c.restore_state[i] != 2) {
				scratch.debug_mesh_current_global_car_index = c.global_start + i;
				car_views[i].sample_old_corner_collision_surface(scratch);
			}
		}
		scratch.debug_mesh_current_global_car_index = -1;

		for (int i = 0; i < count; ++i) {
			if ((c.state_2[i] & 0x8u) && c.restore_state[i] != 2) {
				scratch.debug_mesh_current_global_car_index = c.global_start + i;
				const int corner_collision_type_flag = car_views[i].update_machine_corners(scratch);
				const SimVec3 rail_push = LOAD_INDEXED_VEC3(c, collision_push_rail, i);
				const SimVec3 track_push = LOAD_INDEXED_VEC3(c, collision_push_track, i);
				const SimVec3 velocity = LOAD_INDEXED_VEC3(c, velocity, i);
				const float push_magnitude_rail = rail_push.length();
				const float push_magnitude_track = track_push.length();
				const float current_world_speed = velocity.length();
				float speed_over_weight = 0.0f;
				if (std::abs(c.stat_weight[i]) > 0.0001f) {
					speed_over_weight = current_world_speed / c.stat_weight[i];
				}
				const bool significant_collision =
					push_magnitude_rail > 0.0046296296f && speed_over_weight > 0.0046296296f;
				const bool full_response =
					c.frames_since_start_2[i] > 0x3c && significant_collision &&
					(corner_collision_type_flag & 2) &&
					(c.machine_state[i] & MACHINESTATE::LOWGRIP) == 0;
				const bool landing_response =
					(c.machine_state[i] & MACHINESTATE::JUSTLANDED) &&
					speed_over_weight >= 0.0462962962962f;
				if (push_magnitude_track > 0.0023148148f || push_magnitude_rail > 0.0023148148f ||
					full_response || landing_response) {
					car_views[i].apply_machine_collision_response_from_corners(corner_collision_type_flag,
						push_magnitude_rail, push_magnitude_track, current_world_speed, speed_over_weight, false);
				}
				if (c.machine_state[i] & MACHINESTATE::JUSTLANDED) {
					c.air_time[i] = 0;
				}
			}
		}
		scratch.debug_mesh_current_global_car_index = -1;

		project_startup_velocity_and_speed_soa(c, count);

		update_damage_visual_geometry_soa(c, count);

		for (int i = 0; i < count; ++i) {
			if (c.restore_state[i] == 2) {
				continue;
			}
			car_views[i].handle_machine_damage_and_visuals_tail();
			if (c.frames_since_start_2[i] == 0) {
				STORE_INDEXED_VEC3(c, velocity, i, SimVec3());
				STORE_INDEXED_VEC3(c, position_current, i, LOAD_INDEXED_VEC3(c, initial_pos, i));
			}
		}

		for (int i = 0; i < count; ++i) {
			if (c.restore_state[i] == 2) {
				continue;
			}
			car_views[i].handle_checkpoints(scratch);
			if ((c.machine_state[i] & MACHINESTATE::AIRBORNE) == 0 && (c.machine_state[i] & MACHINESTATE::ZEROHP) == 0) {
				c.last_ground_distance[i] = c.checkpoint_track_distance[i];
				c.last_ground_checkpoint[i] = c.current_checkpoint[i];
			}
		}

		for (int i = 0; i < count; ++i) {
			if (!s_boost_enabled) {
				pending_s_boost_sparks[i] = 0;
				c.s_boost_pending_spark_spawns[i] = 0;
				c.pending_super_sparks[i] = 0;
				continue;
			}
			uint16_t pending = static_cast<uint16_t>(c.s_boost_pending_spark_spawns[i]) + c.pending_super_sparks[i];
			pending_s_boost_sparks[i] = static_cast<uint8_t>(pending > 255 ? 255 : pending);
			c.s_boost_pending_spark_spawns[i] = 0;
			c.pending_super_sparks[i] = 0;
		}
	}

	static inline bool vehicle_motion_active(const PhysicsCarSoA& c, int i)
	{
		return c.restore_state[i] != 2;
	}

	static void apply_vehicle_motion_inputs_soa(PhysicsCarSoA& c, PlayerInput* inputs, int count)
	{
		for (int i = 0; i < count; ++i) {
			if (!vehicle_motion_active(c, i)) {
				continue;
			}

			const PlayerInput& in = inputs[i];
			c.input_steer_yaw[i] = in.steer_horizontal * std::abs(in.steer_horizontal);
			c.input_steer_pitch[i] = -in.steer_vertical;

			const float strafe_left = std::min(1.0f, in.strafe_left * 1.25f);
			const float strafe_right = std::min(1.0f, in.strafe_right * 1.25f);
			c.input_strafe[i] = -strafe_left + strafe_right;

			const float old_accel = c.input_accel[i];
			c.input_accel[i] = in.accelerate;
			const bool accel_just_pressed = c.input_accel[i] > 0.5f && old_accel <= 0.5f;
			c.input_brake[i] = in.brake;

			if (strafe_left > 0.05f && strafe_right > 0.05f) {
				c.machine_state[i] |= MACHINESTATE::MANUAL_DRIFT;
			}
			if (accel_just_pressed) {
				c.machine_state[i] |= MACHINESTATE::JUSTTAPPEDACCEL | MACHINESTATE::B14;
			}
			c.state_2[i] |= 8u;
		}
	}

	static void prepare_vehicle_floor_phase(PhysicsCarSoA& c, PhysicsCar* car_views, int count, TrackQueryScratch &scratch)
	{
		scratch.reset_trigger_events();
		for (int i = 0; i < count; ++i) {
			if (!vehicle_motion_active(c, i)) {
				continue;
			}

			scratch.debug_mesh_current_global_car_index = c.global_start + i;
			const uint32_t old_terrain_state = c.terrain_state[i];
			const SimVec3 trigger_p0 = LOAD_INDEXED_VEC3(c, position_old, i);
			const SimVec3 trigger_p1 = LOAD_INDEXED_VEC3(c, position_current, i);
			const SimVec3 ground_normal = car_views[i].prepare_machine_frame(scratch);
			const bool has_floor = car_views[i].find_floor_beneath_machine(scratch);
			if (has_floor) {
				if ((c.machine_state[i] & MACHINESTATE::AIRBORNE) == 0) {
					bool use_analytic_floor_normal = true;
					const bool use_corner_floor_normal = (c.machine_state[i] & MACHINESTATE::ACTIVE) != 0;
					RaceTrack *track = c.current_track[i];
					if (track && c.current_checkpoint[i] < track->num_checkpoints) {
						const TrackSegment &segment = track->segments[track->checkpoints[c.current_checkpoint[i]].road_segment];
						use_analytic_floor_normal = segment.analytic_collision_enabled;
					}
					if (use_analytic_floor_normal && use_corner_floor_normal) {
						STORE_INDEXED_VEC3(c, track_surface_normal, i, ground_normal);
					}
				} else {
					if (c.height_above_track[i] >= 16.0f) {
						c.machine_state[i] &= ~(MACHINESTATE::AIRBORNE | MACHINESTATE::AIRBORNEMORE0_2S_Q);
					}
				}
			} else {
				const int base = i * 4;
				for (int lane = 0; lane < 4; ++lane) {
					const int p = base + lane;
					c.tilt_force[p] = 0.0f;
					c.tilt_force_spatial_x[p] = 0.0f;
					c.tilt_force_spatial_y[p] = 0.0f;
					c.tilt_force_spatial_z[p] = 0.0f;
					c.tilt_force_spatial_len[p] = 0.0f;
					c.tilt_state[p] |= TILTSTATE::DISCONNECTED | TILTSTATE::AIRBORNE;
				}
			}
			if ((c.machine_state[i] & MACHINESTATE::B29) == 0) {
				car_views[i].set_terrain_state_from_track(scratch, trigger_p0, trigger_p1);
			}
			if ((old_terrain_state & TERRAIN::DASH) != 0u) {
				c.machine_state[i] &= ~MACHINESTATE::JUST_HIT_DASHPLATE;
			}
		}
		scratch.debug_mesh_current_global_car_index = -1;
	}

	static void commit_vehicle_trigger_events(PhysicsCarSoA* car_shards, PhysicsCar* cars, int shard_count, TrackQueryScratch* lane_scratch)
	{
		for (int shard = 0; shard < shard_count; ++shard) {
			PhysicsCarSoA& c = car_shards[shard];
			TrackQueryScratch& scratch = lane_scratch[shard];
			for (int e = 0; e < scratch.trigger_event_count; ++e) {
				const TrackQueryScratch::TriggerEvent& event = scratch.trigger_events[e];
				if (event.car_index < 0 || event.car_index >= c.count) {
					continue;
				}
				RaceTrack* track = c.current_track[event.car_index];
				if (!track || event.trigger_index < 0 || event.trigger_index >= track->num_trigger_colliders) {
					continue;
				}
				TriggerCollider* trigger = track->trigger_colliders[event.trigger_index];
				if (!trigger) {
					continue;
				}
				PhysicsCar* car = cars + c.global_start + event.car_index;
				if ((event.collision_flags & 0x2) != 0) {
					trigger->start_touch(car);
				}
				trigger->touch(car);
				if ((event.collision_flags & 0x4) != 0) {
					trigger->end_touch(car);
				}
			}
			scratch.reset_trigger_events();
		}
	}

	static void project_vehicle_velocity_phase(PhysicsCarSoA& c, int count)
	{
		for (int i = 0; i < count; ++i) {
			if (!vehicle_motion_active(c, i)) {
				continue;
			}

			const float vx = c.velocity_x[i];
			const float vy = c.velocity_y[i];
			const float vz = c.velocity_z[i];
			const float c0x = c.basis_physical_c0x[i];
			const float c0y = c.basis_physical_c0y[i];
			const float c0z = c.basis_physical_c0z[i];
			const float c1x = c.basis_physical_c1x[i];
			const float c1y = c.basis_physical_c1y[i];
			const float c1z = c.basis_physical_c1z[i];
			const float c2x = c.basis_physical_c2x[i];
			const float c2y = c.basis_physical_c2y[i];
			const float c2z = c.basis_physical_c2z[i];

			c.velocity_local_x[i] = c0x * vx + c0y * vy + c0z * vz;
			c.velocity_local_y[i] = c1x * vx + c1y * vy + c1z * vz;
			c.velocity_local_z[i] = c2x * vx + c2y * vy + c2z * vz;

			float steer = -(c.input_steer_yaw[i] * c.stat_turn_reaction[i] + c.input_strafe[i] * c.stat_strafe[i]);
			steer = std::clamp(steer, -45.0f, 45.0f);
			const float angle = DEG_TO_RAD * steer;
			const float cs = deterministic_fp::cosf(angle);
			const float sn = deterministic_fp::sinf(angle);

			const float sx = c0x * cs - c2x * sn;
			const float sy = c0y * cs - c2y * sn;
			const float sz = c0z * cs - c2z * sn;
			const float fz_x = c0x * sn + c2x * cs;
			const float fz_y = c0y * sn + c2y * cs;
			const float fz_z = c0z * sn + c2z * cs;

			c.velocity_local_flattened_and_rotated_x[i] = sx * vx + sy * vy + sz * vz;
			c.velocity_local_flattened_and_rotated_y[i] = 0.0f;
			c.velocity_local_flattened_and_rotated_z[i] = fz_x * vx + fz_y * vy + fz_z * vz;
		}
	}

	static void steering_and_suspension_phase(PhysicsCarSoA& c, PhysicsCar* car_views, int count)
	{
		for (int i = 0; i < count; ++i) {
			if (!vehicle_motion_active(c, i) || (c.machine_state[i] & MACHINESTATE::ACTIVE) == 0) {
				continue;
			}

			float strafe_turn_mod = 1.0f;
			const int base = i * 4;
			if (c.tilt_state[base + 0] & TILTSTATE::DRIFT) strafe_turn_mod -= 0.25f;
			if (c.tilt_state[base + 1] & TILTSTATE::DRIFT) strafe_turn_mod -= 0.25f;
			if (c.tilt_state[base + 2] & TILTSTATE::DRIFT) strafe_turn_mod -= 0.25f;
			if (c.tilt_state[base + 3] & TILTSTATE::DRIFT) strafe_turn_mod -= 0.25f;

			float steer_strength =
				(c.stat_turn_movement[i] + strafe_turn_mod * c.stat_strafe_turn[i] * c.input_strafe[i] *
					c.input_steer_yaw[i]) *
				-c.input_steer_yaw[i];
			if (c.machine_state[i] & MACHINESTATE::SIDEATTACKING) {
				steer_strength *= 0.3f;
			}
			c.velocity_angular_y[i] += 1.5f * steer_strength;
			if (std::abs(c.velocity_angular_y[i]) < 1.0f) {
				c.velocity_angular_y[i] = 0.0f;
			}
			c.input_yaw_dupe[i] = c.input_steer_yaw[i];
		}

		for (int i = 0; i < count; ++i) {
			if (vehicle_motion_active(c, i)) {
				car_views[i].handle_suspension_states();
			}
		}

		for (int i = 0; i < count; ++i) {
			if (!vehicle_motion_active(c, i) || c.frames_since_start_2[i] == 0) {
				continue;
			}
			const float initial_angle_vel_y = c.velocity_angular_y[i];
			car_views[i].handle_machine_turn_and_strafe_points4(initial_angle_vel_y);
		}

		for (int i = 0; i < count; ++i) {
			if (!vehicle_motion_active(c, i)) {
				continue;
			}
			if (c.machine_state[i] & MACHINESTATE::AIRBORNEMORE0_2S_Q) {
				c.turning_related[i] *= 0.02f;
			}
			if (std::abs(c.input_strafe[i]) > 0.01f) {
				c.turning_related[i] *= 0.04f;
			}
		}
	}

	static void linear_orientation_drag_phase(PhysicsCarSoA& c, PhysicsCar* car_views, int count)
	{
		for (int i = 0; i < count; ++i) {
			if (vehicle_motion_active(c, i)) {
				car_views[i].handle_linear_velocity();
			}
		}
		for (int i = 0; i < count; ++i) {
			if (vehicle_motion_active(c, i)) {
				car_views[i].handle_angle_velocity();
			}
		}
		for (int i = 0; i < count; ++i) {
			if (vehicle_motion_active(c, i)) {
				car_views[i].handle_airborne_controls();
			}
		}
		for (int i = 0; i < count; ++i) {
			if (vehicle_motion_active(c, i)) {
				car_views[i].orient_vehicle_from_gravity_or_road();
			}
		}
		for (int i = 0; i < count; ++i) {
			if (vehicle_motion_active(c, i)) {
				car_views[i].handle_drag_and_glide_forces();
			}
		}
	}

	static inline bool four_vehicle_motion_active(const PhysicsCarSoA& c, int i)
	{
		return vehicle_motion_active(c, i) && vehicle_motion_active(c, i + 1) &&
			vehicle_motion_active(c, i + 2) && vehicle_motion_active(c, i + 3);
	}

	static void integrate_vehicle_positions_phase(PhysicsCarSoA& c, int count)
	{
		int i = 0;
		for (; i + 3 < count; i += 4) {
			if (!four_vehicle_motion_active(c, i)) {
				for (int lane = i; lane < i + 4; ++lane) {
					if (!vehicle_motion_active(c, lane)) {
						continue;
					}
					const float inv_weight = 1.0f / std::max(c.stat_weight[lane], 0.001f);
					c.position_current_x[lane] += c.velocity_x[lane] * inv_weight + c.knockback_velocity_x[lane];
					c.position_current_y[lane] += c.velocity_y[lane] * inv_weight + c.knockback_velocity_y[lane];
					c.position_current_z[lane] += c.velocity_z[lane] * inv_weight + c.knockback_velocity_z[lane];
					c.knockback_velocity_x[lane] *= 0.93333334f;
					c.knockback_velocity_y[lane] *= 0.93333334f;
					c.knockback_velocity_z[lane] *= 0.93333334f;
				}
				continue;
			}

			const SimFloat4 inv_weight = SimFloat4(1.0f) / sim_max4(sim_load4(c.stat_weight + i), SimFloat4(0.001f));
			const SimFloat4 knockback_x = sim_load4(c.knockback_velocity_x + i);
			const SimFloat4 knockback_y = sim_load4(c.knockback_velocity_y + i);
			const SimFloat4 knockback_z = sim_load4(c.knockback_velocity_z + i);
			sim_store4(c.position_current_x + i, sim_load4(c.position_current_x + i) + sim_load4(c.velocity_x + i) * inv_weight + knockback_x);
			sim_store4(c.position_current_y + i, sim_load4(c.position_current_y + i) + sim_load4(c.velocity_y + i) * inv_weight + knockback_y);
			sim_store4(c.position_current_z + i, sim_load4(c.position_current_z + i) + sim_load4(c.velocity_z + i) * inv_weight + knockback_z);
			const SimFloat4 knockback_decay(0.93333334f);
			sim_store4(c.knockback_velocity_x + i, knockback_x * knockback_decay);
			sim_store4(c.knockback_velocity_y + i, knockback_y * knockback_decay);
			sim_store4(c.knockback_velocity_z + i, knockback_z * knockback_decay);
		}
		for (; i < count; ++i) {
			if (!vehicle_motion_active(c, i)) {
				continue;
			}
			const float inv_weight = 1.0f / std::max(c.stat_weight[i], 0.001f);
			c.position_current_x[i] += c.velocity_x[i] * inv_weight + c.knockback_velocity_x[i];
			c.position_current_y[i] += c.velocity_y[i] * inv_weight + c.knockback_velocity_y[i];
			c.position_current_z[i] += c.velocity_z[i] * inv_weight + c.knockback_velocity_z[i];
			c.knockback_velocity_x[i] *= 0.93333334f;
			c.knockback_velocity_y[i] *= 0.93333334f;
			c.knockback_velocity_z[i] *= 0.93333334f;
		}
	}

	static void rotate_and_finish_motion_phase(PhysicsCarSoA& c, PhysicsCar* car_views, int count)
	{
		for (int i = 0; i < count; ++i) {
			if (vehicle_motion_active(c, i)) {
				car_views[i].rotate_machine_from_angle_velocity();
			}
		}

		for (int i = 0; i < count; ++i) {
			if (!vehicle_motion_active(c, i)) {
				continue;
			}

			if (c.machine_state[i] & MACHINESTATE::ACTIVE) {
				const uint32_t cd = c.frames_since_start_2[i];
				if (cd < 30) {
					if (cd % 6 == 0) {
						car_views[i].handle_startup_wobble();
					}
				} else if (cd < 90) {
					STORE_INDEXED_VEC3(c, velocity_angular, i, SimVec3());
				}
			}
			if (c.rail_collision_timer[i] > 0) {
				c.rail_collision_timer[i] -= 1;
			}
			c.machine_state[i] &= ~(MACHINESTATE::JUSTHITVEHICLE_Q | MACHINESTATE::LOWGRIP |
				MACHINESTATE::TOOKDAMAGE | MACHINESTATE::B14 |
				MACHINESTATE::MANUAL_DRIFT);

			SimTransform basis = MXT_LOAD_TRANSFORM(c, basis_physical, i);
			basis.orthonormalize();
			MXT_STORE_TRANSFORM(c, basis_physical, i, basis);

			if (c.machine_state[i] & MACHINESTATE::STARTINGCOUNTDOWN) {
				c.machine_state[i] &= ~(MACHINESTATE::RACEJUSTBEGAN_Q | MACHINESTATE::JUSTTAPPEDACCEL);
				SimVec3 road_normal = normalized_or_zero(LOAD_INDEXED_VEC3(c, track_surface_normal, i));
				if (road_normal.length_squared() <= 0.000001f) {
					road_normal = normalized_or_zero(c.road_sample[i].closest_surface.basis.get_column(1));
				}
				if (road_normal.length_squared() <= 0.000001f) {
					road_normal = normalized_or_zero(basis.basis.get_column(1));
				}
				if (road_normal.length_squared() > 0.000001f) {
					const SimVec3 anchor = LOAD_INDEXED_VEC3(c, initial_pos, i);
					const SimVec3 current = LOAD_INDEXED_VEC3(c, position_current, i);
					const SimVec3 tangent_correction = remove_axis_component(current - anchor, road_normal);
					if (tangent_correction.length_squared() > 0.0000001f) {
						STORE_INDEXED_VEC3(c, position_current, i, current - tangent_correction);
						STORE_INDEXED_VEC3(c, position_old, i, LOAD_INDEXED_VEC3(c, position_old, i) - tangent_correction);
						STORE_INDEXED_VEC3(c, position_old_2, i, LOAD_INDEXED_VEC3(c, position_old_2, i) - tangent_correction);
						STORE_INDEXED_VEC3(c, position_old_dupe, i, LOAD_INDEXED_VEC3(c, position_old_dupe, i) - tangent_correction);
						STORE_INDEXED_VEC3(c, position_bottom, i, LOAD_INDEXED_VEC3(c, position_bottom, i) - tangent_correction);
						translate_contact_points_soa(c, i, -tangent_correction);
					}
					STORE_INDEXED_VEC3(c, velocity, i, keep_axis_component(LOAD_INDEXED_VEC3(c, velocity, i), road_normal));
					STORE_INDEXED_VEC3(c, knockback_velocity, i, keep_axis_component(LOAD_INDEXED_VEC3(c, knockback_velocity, i), road_normal));
				}
			}

			if ((c.machine_state[i] & MACHINESTATE::STARTINGCOUNTDOWN) == 0) {
				c.position_bottom_x[i] += c.position_current_x[i] - c.position_old_x[i];
				c.position_bottom_y[i] += c.position_current_y[i] - c.position_old_y[i];
				c.position_bottom_z[i] += c.position_current_z[i] - c.position_old_z[i];
			}
		}
	}

	static void begin_vehicle_motion_phased_soa(PhysicsCarSoA& c, PhysicsCar* car_views, PlayerInput* inputs,
		int count, TrackQueryScratch &scratch)
	{
		apply_vehicle_motion_inputs_soa(c, inputs, count);
		prepare_vehicle_floor_phase(c, car_views, count, scratch);
	}

	static void finish_vehicle_motion_phased_soa(PhysicsCarSoA& c, PhysicsCar* car_views, int count)
	{
		project_vehicle_velocity_phase(c, count);
		steering_and_suspension_phase(c, car_views, count);
		linear_orientation_drag_phase(c, car_views, count);
		integrate_vehicle_positions_phase(c, count);
		rotate_and_finish_motion_phase(c, car_views, count);
	}

	static inline bool intervals_overlap(float min_a, float max_a, float min_b, float max_b)
	{
		return min_a <= max_b && min_b <= max_a;
	}

	static inline SimVec3 transform_point_components(
		float c0x, float c0y, float c0z,
		float c1x, float c1y, float c1z,
		float c2x, float c2y, float c2z,
		float ox, float oy, float oz,
		const SimVec3& p)
	{
		return SimVec3(
			c0x * p.x + c1x * p.y + c2x * p.z + ox,
			c0y * p.x + c1y * p.y + c2y * p.z + oy,
			c0z * p.x + c1z * p.y + c2z * p.z + oz);
	}

	static inline SimVec3x4 transform_points_components4(
		float c0x, float c0y, float c0z,
		float c1x, float c1y, float c1z,
		float c2x, float c2y, float c2z,
		float ox, float oy, float oz,
		SimFloat4 px, SimFloat4 py, SimFloat4 pz)
	{
		return SimVec3x4(
			SimFloat4(c0x) * px + SimFloat4(c1x) * py + SimFloat4(c2x) * pz + SimFloat4(ox),
			SimFloat4(c0y) * px + SimFloat4(c1y) * py + SimFloat4(c2y) * pz + SimFloat4(oy),
			SimFloat4(c0z) * px + SimFloat4(c1z) * py + SimFloat4(c2z) * pz + SimFloat4(oz));
	}

	static void update_damage_visual_geometry_soa(PhysicsCarSoA& c, int count)
	{
		for (int i = 0; i < count; ++i) {
			if ((c.state_2[i] & 0x8u) == 0) {
				continue;
			}

			const float c0x = c.basis_physical_c0x[i];
			const float c0y = c.basis_physical_c0y[i];
			const float c0z = c.basis_physical_c0z[i];
			const float c1x = c.basis_physical_c1x[i];
			const float c1y = c.basis_physical_c1y[i];
			const float c1z = c.basis_physical_c1z[i];
			const float c2x = c.basis_physical_c2x[i];
			const float c2y = c.basis_physical_c2y[i];
			const float c2z = c.basis_physical_c2z[i];
			const float ox = c.position_current_x[i];
			const float oy = c.position_current_y[i];
			const float oz = c.position_current_z[i];

			const int p = i * 4;
			sim_store4(c.tilt_pos_old_x + p, sim_load4(c.tilt_pos_x + p));
			sim_store4(c.tilt_pos_old_y + p, sim_load4(c.tilt_pos_y + p));
			sim_store4(c.tilt_pos_old_z + p, sim_load4(c.tilt_pos_z + p));
			const SimVec3x4 tilt_pos = transform_points_components4(
				c0x, c0y, c0z, c1x, c1y, c1z, c2x, c2y, c2z, ox, oy, oz,
				sim_load4(c.tilt_offset_x + p),
				sim_load4(c.tilt_offset_y + p) + sim_load4(c.tilt_force + p) - sim_load4(c.tilt_rest_length + p),
				sim_load4(c.tilt_offset_z + p));
			sim_store4(c.tilt_pos_x + p, tilt_pos.x);
			sim_store4(c.tilt_pos_y + p, tilt_pos.y);
			sim_store4(c.tilt_pos_z + p, tilt_pos.z);

			sim_store4(c.wall_pos_a_x + p, sim_load4(c.wall_pos_b_x + p));
			sim_store4(c.wall_pos_a_y + p, sim_load4(c.wall_pos_b_y + p));
			sim_store4(c.wall_pos_a_z + p, sim_load4(c.wall_pos_b_z + p));
			const SimVec3x4 wall_pos = transform_points_components4(
				c0x, c0y, c0z, c1x, c1y, c1z, c2x, c2y, c2z, ox, oy, oz,
				sim_load4(c.wall_offset_x + p),
				sim_load4(c.wall_offset_y + p),
				sim_load4(c.wall_offset_z + p));
			sim_store4(c.wall_pos_b_x + p, wall_pos.x);
			sim_store4(c.wall_pos_b_y + p, wall_pos.y);
			sim_store4(c.wall_pos_b_z + p, wall_pos.z);
		}
	}

	static void project_startup_velocity_and_speed_soa(PhysicsCarSoA& c, int count)
	{
		int i = 0;
		for (; i + 3 < count; i += 4) {
			const SimFloat4 vx = sim_load4(c.velocity_x + i);
			const SimFloat4 vy = sim_load4(c.velocity_y + i);
			const SimFloat4 vz = sim_load4(c.velocity_z + i);
			const SimFloat4 nx = sim_load4(c.track_surface_normal_x + i);
			const SimFloat4 ny = sim_load4(c.track_surface_normal_y + i);
			const SimFloat4 nz = sim_load4(c.track_surface_normal_z + i);
			const SimFloat4 startup_mask(
				c.frames_since_start_2[i + 0] <= 90 ? 1.0f : 0.0f,
				c.frames_since_start_2[i + 1] <= 90 ? 1.0f : 0.0f,
				c.frames_since_start_2[i + 2] <= 90 ? 1.0f : 0.0f,
				c.frames_since_start_2[i + 3] <= 90 ? 1.0f : 0.0f);
			const SimFloat4 dot = (vx * nx + vy * ny + vz * nz) * startup_mask;
			const SimFloat4 out_x = vx - nx * dot;
			const SimFloat4 out_y = vy - ny * dot;
			const SimFloat4 out_z = vz - nz * dot;
			sim_store4(c.velocity_x + i, out_x);
			sim_store4(c.velocity_y + i, out_y);
			sim_store4(c.velocity_z + i, out_z);

			const SimFloat4 speed = sim_sqrt4(out_x * out_x + out_y * out_y + out_z * out_z);
			const SimFloat4 inv_weight(
				std::abs(c.stat_weight[i + 0]) > 0.0001f ? 1.0f / c.stat_weight[i + 0] : 0.0f,
				std::abs(c.stat_weight[i + 1]) > 0.0001f ? 1.0f / c.stat_weight[i + 1] : 0.0f,
				std::abs(c.stat_weight[i + 2]) > 0.0001f ? 1.0f / c.stat_weight[i + 2] : 0.0f,
				std::abs(c.stat_weight[i + 3]) > 0.0001f ? 1.0f / c.stat_weight[i + 3] : 0.0f);
			const SimFloat4 countdown_mask(
				(c.machine_state[i + 0] & MACHINESTATE::STARTINGCOUNTDOWN) == 0 ? 1.0f : 0.0f,
				(c.machine_state[i + 1] & MACHINESTATE::STARTINGCOUNTDOWN) == 0 ? 1.0f : 0.0f,
				(c.machine_state[i + 2] & MACHINESTATE::STARTINGCOUNTDOWN) == 0 ? 1.0f : 0.0f,
				(c.machine_state[i + 3] & MACHINESTATE::STARTINGCOUNTDOWN) == 0 ? 1.0f : 0.0f);
			const SimFloat4 old_speed = sim_load4(c.speed_kmh + i);
			const SimFloat4 new_speed = speed * inv_weight * SimFloat4(216.0f);
			sim_store4(c.speed_kmh + i, old_speed + (new_speed - old_speed) * countdown_mask);
		}

		for (; i < count; ++i) {
			const float startup_mask = c.frames_since_start_2[i] <= 90 ? 1.0f : 0.0f;
			const float dot =
				(c.velocity_x[i] * c.track_surface_normal_x[i] +
				 c.velocity_y[i] * c.track_surface_normal_y[i] +
				 c.velocity_z[i] * c.track_surface_normal_z[i]) * startup_mask;
			c.velocity_x[i] -= c.track_surface_normal_x[i] * dot;
			c.velocity_y[i] -= c.track_surface_normal_y[i] * dot;
			c.velocity_z[i] -= c.track_surface_normal_z[i] * dot;
			if ((c.machine_state[i] & MACHINESTATE::STARTINGCOUNTDOWN) == 0) {
				const float speed_sq =
					c.velocity_x[i] * c.velocity_x[i] +
					c.velocity_y[i] * c.velocity_y[i] +
					c.velocity_z[i] * c.velocity_z[i];
				const float inv_weight = std::abs(c.stat_weight[i]) > 0.0001f ? 1.0f / c.stat_weight[i] : 0.0f;
				c.speed_kmh[i] = 216.0f * std::sqrt(speed_sq) * inv_weight;
			}
		}
	}

	static void handle_vehicle_collision_result(GameSim& sim, PhysicsCar* car_views, int i, int j)
	{
		PhysicsCarSoA& car_a = *car_views[i].soa;
		PhysicsCarSoA& car_b = *car_views[j].soa;
		const int lane_a = car_views[i].soa_index;
		const int lane_b = car_views[j].soa_index;
		const uint32_t current_tick = static_cast<uint32_t>(car_a.simulation_tick[lane_a]);
		constexpr uint32_t kCollisionSparkCooldownFrames = 30;
		auto is_recent_hit = [&](PhysicsCarSoA& c, int lane) -> bool {
			if (!c.has_last_hit_tick[lane]) {
				return false;
			}
			const uint32_t delta = current_tick - c.last_hit_tick[lane];
			return delta < kCollisionSparkCooldownFrames;
		};

		const bool recently_hit = is_recent_hit(car_a, lane_a) || is_recent_hit(car_b, lane_b);
		const bool a_attacking = (car_a.machine_state[lane_a] & (MACHINESTATE::SIDEATTACKING | MACHINESTATE::SPINATTACKING)) != 0;
		const bool b_attacking = (car_b.machine_state[lane_b] & (MACHINESTATE::SIDEATTACKING | MACHINESTATE::SPINATTACKING)) != 0;
		int sparks_a = 3;
		int sparks_b = 3;
		if (a_attacking && b_attacking) {
			sparks_a = 6;
			sparks_b = 6;
		} else if (a_attacking && !b_attacking) {
			sparks_b = 8;
		} else if (!a_attacking && b_attacking) {
			sparks_a = 8;
		}
		if (!recently_hit) {
			if (sparks_a > 0) {
				sim.emit_super_sparks_from_car(car_views[i], sparks_a);
			}
			if (sparks_b > 0) {
				sim.emit_super_sparks_from_car(car_views[j], sparks_b);
			}
		}
		car_a.last_hit_tick[lane_a] = current_tick;
		car_b.last_hit_tick[lane_b] = current_tick;
		car_a.has_last_hit_tick[lane_a] = true;
		car_b.has_last_hit_tick[lane_b] = true;
	}

	static void collide_vehicles_broadphase(GameSim& sim, PhysicsCar* car_views, int count,
		int* indices, float* min_x, float* max_x, float* min_y, float* max_y, float* min_z, float* max_z)
	{
		constexpr float kMachineCollisionRadius = 2.0f;
		constexpr float kMutationSlop = 8.0f;

		for (int i = 0; i < count; ++i) {
			PhysicsCarSoA& c = *car_views[i].soa;
			const int lane = car_views[i].soa_index;
			c.position_collision_snapshot_x[lane] = c.position_current_x[lane];
			c.position_collision_snapshot_y[lane] = c.position_current_y[lane];
			c.position_collision_snapshot_z[lane] = c.position_current_z[lane];
			const float radius = kMachineCollisionRadius;
			const float extent = radius + c.speed_kmh[lane] / 216.0f + kMutationSlop;
			indices[i] = i;
			min_x[i] = c.position_current_x[lane] - extent;
			max_x[i] = c.position_current_x[lane] + extent;
			min_y[i] = c.position_current_y[lane] - extent;
			max_y[i] = c.position_current_y[lane] + extent;
			min_z[i] = c.position_current_z[lane] - extent;
			max_z[i] = c.position_current_z[lane] + extent;
		}

		std::sort(indices, indices + count, [&](int a, int b) {
			if (min_x[a] != min_x[b]) {
				return min_x[a] < min_x[b];
			}
			return a < b;
		});

		for (int sorted_i = 0; sorted_i < count; ++sorted_i) {
			const int i = indices[sorted_i];
			const float max_i_x = max_x[i];
			for (int sorted_j = sorted_i + 1; sorted_j < count; ++sorted_j) {
				const int j = indices[sorted_j];
				if (min_x[j] > max_i_x) {
					break;
				}
				if (!intervals_overlap(min_y[i], max_y[i], min_y[j], max_y[j]) ||
					!intervals_overlap(min_z[i], max_z[i], min_z[j], max_z[j])) {
					continue;
				}
				if (car_views[i].handle_machine_v_machine_collision(car_views[j])) {
					handle_vehicle_collision_result(sim, car_views, i, j);
				}
			}
		}
	}
}

void GameSim::_bind_methods()
{
	ClassDB::bind_method(D_METHOD("instantiate_gamesim", "lvldat_buf", "car_prop_buffers", "accel_settings"), &GameSim::instantiate_gamesim);
	ClassDB::bind_method(D_METHOD("destroy_gamesim"), &GameSim::destroy_gamesim);
	ClassDB::bind_method(D_METHOD("render_gamesim"), &GameSim::render_gamesim);
	ClassDB::bind_method(D_METHOD("get_sim_started"), &GameSim::get_sim_started);
	ClassDB::bind_method(D_METHOD("set_sim_started", "p_sim_started"), &GameSim::set_sim_started);
	ClassDB::bind_method(D_METHOD("set_spawn_seed", "seed"), &GameSim::set_spawn_seed);
	ClassDB::bind_method(D_METHOD("set_start_grid_slots", "slots"), &GameSim::set_start_grid_slots);
	ClassDB::bind_method(D_METHOD("set_vehicle_restore_enabled", "enabled"), &GameSim::set_vehicle_restore_enabled);
	ClassDB::bind_method(D_METHOD("get_vehicle_restore_enabled"), &GameSim::get_vehicle_restore_enabled);
	ClassDB::bind_method(D_METHOD("set_multiplayer_intro_camera_enabled", "enabled"), &GameSim::set_multiplayer_intro_camera_enabled);
	ClassDB::bind_method(D_METHOD("get_multiplayer_intro_camera_enabled"), &GameSim::get_multiplayer_intro_camera_enabled);
	ClassDB::bind_method(D_METHOD("set_bumpers_enabled", "enabled"), &GameSim::set_bumpers_enabled);
	ClassDB::bind_method(D_METHOD("get_bumpers_enabled"), &GameSim::get_bumpers_enabled);
	ClassDB::bind_method(D_METHOD("set_s_boost_enabled", "enabled"), &GameSim::set_s_boost_enabled);
	ClassDB::bind_method(D_METHOD("get_s_boost_enabled"), &GameSim::get_s_boost_enabled);
	ClassDB::bind_method(D_METHOD("save_state"), &GameSim::save_state);
	ClassDB::bind_method(D_METHOD("load_state", "target_tick"), &GameSim::load_state);
	ClassDB::bind_method(D_METHOD("load_state_data", "target_tick", "data"), &GameSim::load_state_data);
	ClassDB::bind_method(D_METHOD("get_state_data", "target_tick"), &GameSim::get_state_data);
	ClassDB::bind_method(D_METHOD("get_network_state_size_stats"), &GameSim::get_network_state_size_stats);
	ClassDB::bind_method(D_METHOD("set_state_data", "target_tick", "data"), &GameSim::set_state_data);
	ClassDB::bind_method(D_METHOD("render_gamesim_visuals_only", "process_delta"), &GameSim::render_gamesim_visuals_only);
	ClassDB::bind_method(D_METHOD("get_dip_switches"), &GameSim::get_dip_switches);
	ClassDB::bind_method(D_METHOD("is_dip_switch_enabled", "flag"), &GameSim::is_dip_switch_enabled);
	ClassDB::bind_method(D_METHOD("set_dip_switch_enabled", "flag", "enabled"), &GameSim::set_dip_switch_enabled);
	ClassDB::bind_method(D_METHOD("get_first_lap_distance"), &GameSim::get_first_lap_distance);
	ClassDB::bind_method(D_METHOD("get_track_lap_length"), &GameSim::get_track_lap_length);
	ClassDB::bind_method(D_METHOD("set_cpu_driver_manager", "manager"), &GameSim::set_cpu_driver_manager);
	ClassDB::bind_method(D_METHOD("get_cpu_driver_manager"), &GameSim::get_cpu_driver_manager);
	ClassDB::bind_method(D_METHOD("get_native_cpu_input_for_tick", "player_id", "expected_tick"), &GameSim::get_native_cpu_input_for_tick);
	ClassDB::bind_method(D_METHOD("get_input_frame_as_dictionary", "target_tick"), &GameSim::get_input_frame_as_dictionary);
	ClassDB::bind_method(D_METHOD("set_player_metadata", "player_ids", "cpu_flags"), &GameSim::set_player_metadata);
	ClassDB::bind_method(D_METHOD("get_phase_profile_string"), &GameSim::get_phase_profile_string);
	ClassDB::bind_method(D_METHOD("get_render_profile_string"), &GameSim::get_render_profile_string);
	ClassDB::bind_method(D_METHOD("set_render_profile_enabled", "enabled"), &GameSim::set_render_profile_enabled);
	ClassDB::bind_method(D_METHOD("set_render_node_effects_enabled", "enabled"), &GameSim::set_render_node_effects_enabled);
	ClassDB::bind_method(D_METHOD("set_render_thruster_lights_enabled", "enabled"), &GameSim::set_render_thruster_lights_enabled);
	ClassDB::bind_method(D_METHOD("get_player_race_place", "player_id"), &GameSim::get_player_race_place);
	ClassDB::bind_method(D_METHOD("get_race_leaderboard_window", "player_id", "max_entries"), &GameSim::get_race_leaderboard_window);
	ClassDB::bind_method(D_METHOD("is_player_race_finished", "player_id"), &GameSim::is_player_race_finished);
	ClassDB::bind_method(D_METHOD("is_player_race_eliminated", "player_id"), &GameSim::is_player_race_eliminated);
	ClassDB::bind_method(D_METHOD("get_player_ko_energy_bonus", "player_id"), &GameSim::get_player_ko_energy_bonus);
	ClassDB::bind_method(D_METHOD("set_player_ko_energy_bonus", "player_id", "bonus"), &GameSim::set_player_ko_energy_bonus);
	ClassDB::bind_method(D_METHOD("get_player_lap_distance", "player_id"), &GameSim::get_player_lap_distance);
	ClassDB::bind_method(D_METHOD("get_player_lap", "player_id"), &GameSim::get_player_lap);
	ClassDB::bind_method(D_METHOD("get_player_level_start_time", "player_id"), &GameSim::get_player_level_start_time);
	ClassDB::bind_method(D_METHOD("get_player_debug_string", "player_id"), &GameSim::get_player_debug_string);
	ClassDB::bind_method(D_METHOD("get_bumper_debug_string"), &GameSim::get_bumper_debug_string);
	ClassDB::bind_method(D_METHOD("get_race_order"), &GameSim::get_race_order);
	ClassDB::bind_method(D_METHOD("get_player_render_transform", "player_id"), &GameSim::get_player_render_transform);
	ClassDB::bind_method(D_METHOD("get_car_render_transform", "car_index"), &GameSim::get_car_render_transform);
	ClassDB::bind_method(D_METHOD("get_saved_player_voice_transform", "player_id", "target_tick"), &GameSim::get_saved_player_voice_transform);
	ClassDB::bind_method(D_METHOD("get_saved_player_voice_transforms", "target_tick"), &GameSim::get_saved_player_voice_transforms);
	ClassDB::bind_method(D_METHOD("get_check_warning_candidates", "player_id"), &GameSim::get_check_warning_candidates);
	ClassDB::bind_method(D_METHOD("consume_race_events"), &GameSim::consume_race_events);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "sim_started"), "set_sim_started", "get_sim_started");
	ClassDB::bind_method(D_METHOD("get_car_node_container"), &GameSim::get_car_node_container);
	ClassDB::bind_method(D_METHOD("set_car_node_container", "p_car_node_container"), &GameSim::set_car_node_container);
	ClassDB::bind_method(D_METHOD("get_spark_node_container"), &GameSim::get_spark_node_container);
	ClassDB::bind_method(D_METHOD("set_spark_node_container", "p_spark_node_container"), &GameSim::set_spark_node_container);
	ClassDB::bind_method(D_METHOD("set_car_render_manager", "p_car_render_manager"), &GameSim::set_car_render_manager);
	ClassDB::bind_method(D_METHOD("set_gameplay_camera", "p_camera", "player_id"), &GameSim::set_gameplay_camera);
	ClassDB::bind_method(D_METHOD("tick_singleplayer", "local_player_id", "local_input"), &GameSim::tick_singleplayer);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "car_node_container", PROPERTY_HINT_RESOURCE_TYPE, "Node3D"), "set_car_node_container", "get_car_node_container");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "spark_node_container", PROPERTY_HINT_RESOURCE_TYPE, "Node3D"), "set_spark_node_container", "get_spark_node_container");
};

GameSim::GameSim()
{
	tick = 0;
	tick_delta = 1.0f / 60.0f;
	sim_started = false;
	car_node_container = nullptr;
	spark_node_container = nullptr;
	super_spark_state = nullptr;
	super_sparks = nullptr;
	spark_multimesh_instance = nullptr;
	num_cars = 0;
	cars = nullptr;
	car_properties_array = nullptr;
	bumper_cars = nullptr;
	bumper_properties_array = nullptr;
	reset_super_sparks();
	for (int i = 0; i < STATE_BUFFER_LEN; i++)
	{
		state_buffer[i].data = nullptr;
		state_buffer[i].size = 0;
		state_buffer[i].bumper_state_count = 0;
		state_buffer[i].bumper_scheduler_lap = 0;
		state_buffer[i].bumper_next_sequence = 0;
		state_buffer[i].tick = -1;
		state_buffer[i].voice_transform_count = 0;
	}
	input_buffer = nullptr;
};

GameSim::~GameSim()
{
	stop_vehicle_lane_workers();
	destroy_gamesim();
	free_vehicle_tick_soa();
	for (int i = 0; i < STATE_BUFFER_LEN; i++)
	{
		if (state_buffer[i].data)
		{
			::free(state_buffer[i].data);
			state_buffer[i].data = nullptr;
		}
		state_buffer[i].size = 0;
		state_buffer[i].tick = -1;
		state_buffer[i].voice_transform_count = 0;
		state_buffer[i].voice_transforms.clear();
	}
	if (input_buffer) {
		::free(input_buffer);
		input_buffer = nullptr;
	}
};

void GameSim::VehicleLaneGroup::reset(int p_count)
{
	std::lock_guard<std::mutex> lock(mutex);
	count = p_count;
	waiting = 0;
	generation = 0;
}

void GameSim::VehicleLaneGroup::sync()
{
	if (count <= 1) {
		return;
	}
	std::unique_lock<std::mutex> lock(mutex);
	const uint32_t local_generation = generation;
	waiting += 1;
	if (waiting == count) {
		waiting = 0;
		generation += 1;
		cv.notify_all();
		return;
	}
	cv.wait(lock, [&]() {
		return generation != local_generation;
	});
}

void GameSim::ensure_vehicle_lane_workers()
{
	if (vehicle_lane_workers_started) {
		return;
	}
	vehicle_lane_stop = false;
	for (int i = 0; i < VEHICLE_WORKER_COUNT - 1; ++i) {
		const int lane = i + 1;
		vehicle_lane_workers[i] = std::thread([this, lane]() {
			uint32_t seen_generation = 0;
			for (;;) {
				std::unique_lock<std::mutex> lock(vehicle_lane_mutex);
				vehicle_lane_cv.wait(lock, [&]() {
					return vehicle_lane_stop || vehicle_lane_generation != seen_generation;
				});
				if (vehicle_lane_stop) {
					return;
				}
				seen_generation = vehicle_lane_generation;
				const bool should_run = lane < vehicle_lane_active_count;
				auto fn = vehicle_lane_fn;
				lock.unlock();

				if (should_run) {
					fn(lane, vehicle_lane_group);
				}

				if (should_run) {
					lock.lock();
					vehicle_lane_pending -= 1;
					if (vehicle_lane_pending == 0) {
						vehicle_lane_done_cv.notify_one();
					}
				}
			}
		});
	}
	vehicle_lane_workers_started = true;
}

void GameSim::stop_vehicle_lane_workers()
{
	if (!vehicle_lane_workers_started) {
		return;
	}
	{
		std::lock_guard<std::mutex> lock(vehicle_lane_mutex);
		vehicle_lane_stop = true;
		vehicle_lane_generation += 1;
	}
	vehicle_lane_cv.notify_all();
	for (int i = 0; i < VEHICLE_WORKER_COUNT - 1; ++i) {
		if (vehicle_lane_workers[i].joinable()) {
			vehicle_lane_workers[i].join();
		}
	}
	vehicle_lane_fn = nullptr;
	vehicle_lane_active_count = 0;
	vehicle_lane_pending = 0;
	vehicle_lane_workers_started = false;
	vehicle_lane_stop = false;
}

void GameSim::run_vehicle_lanes(int lane_count, bool parallel, const std::function<void(int, VehicleLaneGroup&)>& fn)
{
	if (!parallel || lane_count <= 1) {
		VehicleLaneGroup group;
		group.reset(1);
		for (int lane = 0; lane < lane_count; ++lane) {
			fn(lane, group);
		}
		return;
	}

	const int active_lanes = std::min(lane_count, VEHICLE_WORKER_COUNT);
	ensure_vehicle_lane_workers();
	vehicle_lane_group.reset(active_lanes);
	{
		std::lock_guard<std::mutex> lock(vehicle_lane_mutex);
		vehicle_lane_fn = fn;
		vehicle_lane_active_count = active_lanes;
		vehicle_lane_pending = active_lanes - 1;
		vehicle_lane_generation += 1;
	}
	vehicle_lane_cv.notify_all();

	fn(0, vehicle_lane_group);

	if (active_lanes > 1) {
		std::unique_lock<std::mutex> lock(vehicle_lane_mutex);
		vehicle_lane_done_cv.wait(lock, [&]() {
			return vehicle_lane_pending == 0;
		});
		vehicle_lane_fn = nullptr;
		vehicle_lane_active_count = 0;
	} else {
		std::lock_guard<std::mutex> lock(vehicle_lane_mutex);
		vehicle_lane_fn = nullptr;
		vehicle_lane_active_count = 0;
	}
}

void GameSim::set_multiplayer_intro_camera_enabled(bool enabled)
{
	multiplayer_intro_camera_enabled = enabled;
	start_countdown_extra_frames = enabled ? 600u : 0u;
}

void GameSim::set_bumpers_enabled(bool enabled)
{
	bumpers_enabled = enabled;
}

void GameSim::set_s_boost_enabled(bool enabled)
{
	s_boost_enabled = enabled;
	if (enabled) {
		return;
	}
	if (super_spark_state && super_sparks) {
		reset_super_sparks();
	}
	if (!cars) {
		return;
	}
	for (int i = 0; i < num_cars; ++i) {
		PhysicsCarSoA& soa = *cars[i].soa;
		const int lane = cars[i].soa_index;
		soa.s_boost_charge[lane] = 0;
		soa.s_boost_active[lane] = false;
		soa.s_boost_frames_remaining[lane] = 0;
		soa.s_boost_emit_frame_accumulator[lane] = 0;
		soa.s_boost_pending_spark_spawns[lane] = 0;
		soa.pending_super_sparks[lane] = 0;
	}
}

void GameSim::configure_bumper_car(int bumper_slot)
{
	if (!bumper_cars || bumper_slot < 0 || bumper_slot >= bumper_count) {
		return;
	}
	PhysicsCarSoA& soa = *bumper_cars[bumper_slot].soa;
	const int lane = bumper_cars[bumper_slot].soa_index;
	PhysicsCarProperties* props = soa.car_properties[lane];
	if (!props) {
		return;
	}
	props->weight_kg = 1800.0f;
	props->acceleration = 0.55f;
	props->max_speed = 0.14f;
	props->grip_1 = 1.2f;
	props->grip_2 = 1.0f;
	props->grip_3 = 0.35f;
	props->turn_tension = 0.02f;
	props->drift_accel = 1.8f;
	props->turn_movement = 240.0f;
	props->strafe_turn = 160.0f;
	props->strafe = 120.0f;
	props->turn_reaction = 45.0f;
	props->boost_strength = 0.0f;
	props->boost_length = 0.1f;
	props->turn_decel = 0.0f;
	props->drag = 0.0065f;
	props->body = 1.0f;
	props->camera_reorienting = 1.0f;
	props->camera_repositioning = 1.0f;
	props->track_collision = 2.0f;
	props->obstacle_collision = 3.5f;
	props->max_energy = 50.0f;
	props->boost_energy_use_rate = 999.0f;
	props->energy_recharge_rate = 0.0f;
	for (int p = 0; p < 4; ++p) {
		props->tilt_corners[p].x *= 1.4f;
		props->tilt_corners[p].z *= 1.4f;
		props->wall_corners[p].x *= 1.4f;
		props->wall_corners[p].z *= 1.4f;
	}
	soa.machine_name[lane] = "Bumper";
	soa.m_accel_setting[lane] = 1.0f;
	if (bumper_slot >= 0 && bumper_slot < bumper_count) {
		bumper_states[bumper_slot].active = 0;
		bumper_states[bumper_slot].spawn_lap = 0;
		bumper_states[bumper_slot].next_sequence = static_cast<uint32_t>(bumper_slot);
		bumper_states[bumper_slot].target_lane = 0.0f;
	}
}

bool GameSim::sample_track_transform_at_distance(float absolute_distance, float lane_offset, SimTransform& out_transform, uint16_t& out_checkpoint, float& out_fraction) const
{
	if (!current_track || current_track->num_checkpoints <= 0) {
		return false;
	}
	float lap_length = current_track->lap_length;
	if (lap_length <= 0.0f) {
		lap_length = current_track->checkpoints[current_track->num_checkpoints - 1].distance;
	}
	if (lap_length <= 0.0f) {
		return false;
	}
	float distance = std::fmod(absolute_distance, lap_length);
	if (distance < 0.0f) {
		distance += lap_length;
	}
	int cp_index = current_track->num_checkpoints - 1;
	for (int i = 0; i < current_track->num_checkpoints; ++i) {
		if (distance <= current_track->checkpoints[i].distance) {
			cp_index = i;
			break;
		}
	}
	const CollisionCheckpoint& cp = current_track->checkpoints[cp_index];
	const float cp_start = std::max(0.0f, cp.distance - cp.local_distance);
	const float cp_len = std::max(cp.local_distance, 0.001f);
	const float cp_fraction = std::clamp((distance - cp_start) / cp_len, 0.0f, 1.0f);
	const float t_y = cp.t_start + (cp.t_end - cp.t_start) * cp_fraction;
	if (cp.road_segment < 0 || cp.road_segment >= current_track->num_segments) {
		return false;
	}
	current_track->segments[cp.road_segment].road_shape->get_oriented_transform_at_time(
		out_transform,
		SimVec2(std::clamp(lane_offset, -0.85f, 0.85f), t_y));
	out_transform.basis.orthonormalize();
	out_checkpoint = static_cast<uint16_t>(cp_index);
	out_fraction = cp_fraction;
	return true;
}

void GameSim::deactivate_bumper_car(int bumper_slot)
{
	if (!bumper_cars || bumper_slot < 0 || bumper_slot >= bumper_count) {
		return;
	}
	const int was_active = bumper_states[bumper_slot].active;
	bumper_states[bumper_slot].active = 0;
	if (was_active) {
		bumper_states[bumper_slot].next_sequence = 0u;
	}
	PhysicsCarSoA& soa = *bumper_cars[bumper_slot].soa;
	const int lane = bumper_cars[bumper_slot].soa_index;
	const SimVec3 hidden(0.0f, current_track ? current_track->minimum_y - 10000.0f : -10000.0f, 0.0f);
	STORE_INDEXED_VEC3(soa, position_current, lane, hidden);
	STORE_INDEXED_VEC3(soa, position_old, lane, hidden);
	STORE_INDEXED_VEC3(soa, position_old_2, lane, hidden);
	STORE_INDEXED_VEC3(soa, position_old_dupe, lane, hidden);
	SimTransform hidden_transform = MXT_LOAD_TRANSFORM(soa, transform_visual, lane);
	hidden_transform.origin = hidden;
	MXT_STORE_TRANSFORM(soa, transform_visual, lane, hidden_transform);
	MXT_STORE_TRANSFORM(soa, basis_physical, lane, hidden_transform);
	MXT_STORE_TRANSFORM(soa, basis_physical_other, lane, hidden_transform);
	STORE_INDEXED_VEC3(soa, position_bottom, lane, hidden);
	STORE_INDEXED_VEC3(soa, position_behind, lane, hidden);
	STORE_INDEXED_VEC3(soa, velocity, lane, SimVec3());
	STORE_INDEXED_VEC3(soa, velocity_local, lane, SimVec3());
	STORE_INDEXED_VEC3(soa, velocity_local_flattened_and_rotated, lane, SimVec3());
	soa.energy[lane] = 0.0f;
	soa.speed_kmh[lane] = 0.0f;
	soa.base_speed[lane] = 0.0f;
	soa.current_track[lane] = nullptr;
	soa.restore_state[lane] = 2;
	soa.restore_wait_frames[lane] = 0;
	soa.restore_move_frames[lane] = 0;
	soa.machine_state[lane] |= MACHINESTATE::ZEROHP;
	soa.machine_state[lane] &= ~(MACHINESTATE::ACTIVE | MACHINESTATE::STARTINGCOUNTDOWN | MACHINESTATE::FALLOUT | MACHINESTATE::AIRBORNE | MACHINESTATE::AIRBORNEMORE0_2S_Q);
}

void GameSim::set_bumper_track_state(int bumper_slot, float absolute_distance, float lane_offset, bool reset_history)
{
	if (!bumper_cars || !current_track || bumper_slot < 0 || bumper_slot >= bumper_count) {
		return;
	}
	SimTransform transform;
	uint16_t checkpoint = 0;
	float checkpoint_fraction = 0.0f;
	if (!sample_track_transform_at_distance(absolute_distance, lane_offset, transform, checkpoint, checkpoint_fraction)) {
		return;
	}
	const SimVec3 surface_origin = transform.origin;
	transform.basis = transform.basis.rotated(transform.basis.get_column(1), Math_PI);
	transform.basis.orthonormalize();
	float lap_length = current_track->lap_length;
	if (lap_length <= 0.0f && current_track->num_checkpoints > 0) {
		lap_length = current_track->checkpoints[current_track->num_checkpoints - 1].distance;
	}
	const float bumper_surface_offset = 0.0f;
	const float bumper_spawn_height = 19.5f;
	transform.origin += transform.basis.get_column(1) * bumper_surface_offset;
	PhysicsCarSoA& soa = *bumper_cars[bumper_slot].soa;
	const int lane = bumper_cars[bumper_slot].soa_index;
	soa.current_track[lane] = current_track;
	soa.restore_state[lane] = 0;
	soa.restore_wait_frames[lane] = 0;
	soa.restore_move_frames[lane] = 0;
	STORE_INDEXED_VEC3(soa, position_current, lane, transform.origin);
	if (reset_history) {
		STORE_INDEXED_VEC3(soa, position_old, lane, transform.origin);
		STORE_INDEXED_VEC3(soa, position_old_2, lane, transform.origin);
		STORE_INDEXED_VEC3(soa, position_old_dupe, lane, transform.origin);
	}
	STORE_INDEXED_VEC3(soa, position_bottom, lane, transform.xform(SimVec3(0.0f, -0.1f, 0.0f)));
	STORE_INDEXED_VEC3(soa, track_surface_normal, lane, transform.basis.get_column(1));
	STORE_INDEXED_VEC3(soa, track_surface_pos, lane, surface_origin);
	MXT_STORE_TRANSFORM(soa, basis_physical, lane, transform);
	if (reset_history) {
		MXT_STORE_TRANSFORM(soa, basis_physical_other, lane, transform);
	}
	MXT_STORE_TRANSFORM(soa, transform_visual, lane, transform);
	if (reset_history) {
		const int point_base = lane * 4;
		const SimVec3 reset_position = transform.origin;
		const SimBasis& reset_basis = transform.basis;
		const SimVec3x4 tilt_pos = transform_points_components4(
			reset_basis.c0.x, reset_basis.c0.y, reset_basis.c0.z,
			reset_basis.c1.x, reset_basis.c1.y, reset_basis.c1.z,
			reset_basis.c2.x, reset_basis.c2.y, reset_basis.c2.z,
			reset_position.x, reset_position.y, reset_position.z,
			sim_load4(soa.tilt_offset_x + point_base),
			sim_load4(soa.tilt_offset_y + point_base),
			sim_load4(soa.tilt_offset_z + point_base));
		const SimVec3x4 wall_pos = transform_points_components4(
			reset_basis.c0.x, reset_basis.c0.y, reset_basis.c0.z,
			reset_basis.c1.x, reset_basis.c1.y, reset_basis.c1.z,
			reset_basis.c2.x, reset_basis.c2.y, reset_basis.c2.z,
			reset_position.x, reset_position.y, reset_position.z,
			sim_load4(soa.wall_offset_x + point_base),
			sim_load4(soa.wall_offset_y + point_base),
			sim_load4(soa.wall_offset_z + point_base));
		const SimVec3 wall_sweep_origin = transform.xform(SimVec3(0.0f, 0.1f, 0.0f));
		for (int point = 0; point < 4; ++point) {
			const int p = point_base + point;
			soa.tilt_state[p] = 0;
			soa.tilt_force[p] = 0.0f;
			soa.tilt_force_spatial_len[p] = 0.0f;
			STORE_INDEXED_VEC3(soa, tilt_force_spatial, p, SimVec3());
			STORE_INDEXED_VEC3(soa, tilt_up_vector_2, p, transform.basis.get_column(1));
			STORE_INDEXED_VEC3(soa, tilt_up_vector, p, transform.basis.get_column(1));
			STORE_INDEXED_VEC3(soa, wall_pos_a, p, wall_sweep_origin);
			STORE_INDEXED_VEC3(soa, wall_collision, p, SimVec3());
		}
		sim_store4(soa.tilt_pos_old_x + point_base, tilt_pos.x);
		sim_store4(soa.tilt_pos_old_y + point_base, tilt_pos.y);
		sim_store4(soa.tilt_pos_old_z + point_base, tilt_pos.z);
		sim_store4(soa.tilt_pos_x + point_base, tilt_pos.x);
		sim_store4(soa.tilt_pos_y + point_base, tilt_pos.y);
		sim_store4(soa.tilt_pos_z + point_base, tilt_pos.z);
		sim_store4(soa.wall_pos_b_x + point_base, wall_pos.x);
		sim_store4(soa.wall_pos_b_y + point_base, wall_pos.y);
		sim_store4(soa.wall_pos_b_z + point_base, wall_pos.z);
	}
	soa.current_checkpoint[lane] = checkpoint;
	soa.current_collision_checkpoint[lane] = checkpoint;
	soa.last_ground_checkpoint[lane] = checkpoint;
	soa.checkpoint_fraction[lane] = checkpoint_fraction;
	if (lap_length > 0.0f) {
		soa.checkpoint_track_distance[lane] = std::fmod(absolute_distance, lap_length);
		soa.lap[lane] = static_cast<uint8_t>(std::clamp(static_cast<int>(absolute_distance / lap_length), 0, 255));
	}
	soa.previous_lap_distance[lane] = current_track->compute_lap_distance(
		soa.current_checkpoint[lane],
		soa.checkpoint_fraction[lane],
		soa.lap[lane]);
	soa.last_ground_distance[lane] = soa.checkpoint_track_distance[lane];
	soa.last_ground_checkpoint[lane] = checkpoint;
	soa.height_above_track[lane] = bumper_spawn_height;
	soa.machine_state[lane] &= ~(MACHINESTATE::FALLOUT | MACHINESTATE::AIRBORNE | MACHINESTATE::AIRBORNEMORE0_2S_Q);
}

void GameSim::update_bumpers(float lead_distance, int leader_lap)
{
	if (!bumpers_enabled || bumper_count <= 0 || !bumper_cars || !current_track) {
		return;
	}
	float lap_length = current_track->lap_length;
	if (lap_length <= 0.0f && current_track->num_checkpoints > 0) {
		lap_length = current_track->checkpoints[current_track->num_checkpoints - 1].distance;
	}
	if (lap_length <= 0.0f) {
		return;
	}
	if (leader_lap < 2) {
		return;
	}
	float lap_distance = std::fmod(lead_distance, lap_length);
	if (lap_distance < 0.0f) {
		lap_distance += lap_length;
	}
	const float interval = leader_lap == 2 ? 520.0f : 300.0f;
	const uint8_t scheduler_lap = static_cast<uint8_t>(std::min(leader_lap, 255));
	if (bumper_scheduler_lap != scheduler_lap) {
		bumper_scheduler_lap = scheduler_lap;
		bumper_next_sequence = 0;
	}
	const int max_active_bumpers = leader_lap == 2 ? 4 : 12;
	int active_count = 0;
	for (int slot = 0; slot < bumper_count && slot < BUMPER_POOL_SIZE; ++slot) {
		BumperState& state = bumper_states[slot];
		if (state.spawn_lap != static_cast<uint8_t>(std::min(leader_lap, 255))) {
			state.spawn_lap = static_cast<uint8_t>(std::min(leader_lap, 255));
		}
		PhysicsCarSoA& soa = *bumper_cars[slot].soa;
		const int lane = bumper_cars[slot].soa_index;
		const float bumper_distance = current_track->compute_lap_distance(
			soa.current_checkpoint[lane],
			soa.checkpoint_fraction[lane],
			soa.lap[lane]);
		if (state.active) {
			if ((soa.machine_state[lane] & MACHINESTATE::ZEROHP) != 0u ||
					lead_distance - bumper_distance > 180.0f) {
				deactivate_bumper_car(slot);
				continue;
			}
		}
		if (state.active) {
			active_count += 1;
		}
	}
	if (active_count >= max_active_bumpers) {
		return;
	}
	const uint32_t sequence_seed = bumper_track_seed ? bumper_track_seed : bumper_track_seed_from_track(current_track);
	const float trigger_distance = bumper_sequence_trigger_distance(
		sequence_seed,
		leader_lap,
		bumper_next_sequence,
		interval,
		lap_length);
	if (lap_distance < trigger_distance || trigger_distance >= lap_length - 80.0f) {
		return;
	}
	for (int slot = 0; slot < bumper_count && slot < BUMPER_POOL_SIZE; ++slot) {
		BumperState& state = bumper_states[slot];
		if (state.active) {
			continue;
		}
		PhysicsCarSoA& soa = *bumper_cars[slot].soa;
		const int lane = bumper_cars[slot].soa_index;
		const uint32_t spawn_sequence = bumper_next_sequence;
		const uint32_t lane_hash = bumper_hash_u32(sequence_seed ^ (spawn_sequence * 0x9E3779B9u) ^ (static_cast<uint32_t>(slot) * 0x85EBCA6Bu));
		state.target_lane = (static_cast<float>(lane_hash & 0xffffu) / 65535.0f) * 1.2f - 0.6f;
		const float spawn_distance = lead_distance + 1000.0f + static_cast<float>(slot % 3) * 38.0f;
		SimTransform spawn_transform;
		uint16_t cp = 0;
		float cp_fraction = 0.0f;
		if (!sample_track_transform_at_distance(spawn_distance, state.target_lane, spawn_transform, cp, cp_fraction)) {
			continue;
		}
		set_bumper_track_state(slot, spawn_distance, state.target_lane, true);
		const SimTransform bumper_transform = MXT_LOAD_TRANSFORM(soa, basis_physical, lane);
		const float bumper_weight = std::max(soa.stat_weight[lane], 0.001f);
		SimTransform forward_sample;
		uint16_t forward_cp = 0;
		float forward_fraction = 0.0f;
		SimVec3 cruise_direction;
		if (sample_track_transform_at_distance(spawn_distance + 5.0f, state.target_lane, forward_sample, forward_cp, forward_fraction)) {
			cruise_direction = forward_sample.origin - spawn_transform.origin;
		}
		if (cruise_direction.length_squared() <= 0.000001f) {
			cruise_direction = -bumper_transform.basis.get_column(2);
		}
		if (cruise_direction.length_squared() <= 0.000001f) {
			cruise_direction = SimVec3(0.0f, 0.0f, -1.0f);
		}
		cruise_direction.normalize();
		const SimVec3 cruise_velocity = cruise_direction * (850.0f / 216.0f) * bumper_weight;
		const SimVec3 cruise_delta = cruise_velocity / bumper_weight;
		const SimVec3 spawn_position = LOAD_INDEXED_VEC3(soa, position_current, lane);
		STORE_INDEXED_VEC3(soa, velocity, lane, cruise_velocity);
		STORE_INDEXED_VEC3(soa, velocity_local, lane, SimVec3(0.0f, 0.0f, -(850.0f / 216.0f) * bumper_weight));
		STORE_INDEXED_VEC3(soa, velocity_local_flattened_and_rotated, lane, SimVec3(0.0f, 0.0f, -(850.0f / 216.0f) * bumper_weight));
		STORE_INDEXED_VEC3(soa, position_old, lane, spawn_position - cruise_delta);
		STORE_INDEXED_VEC3(soa, position_old_2, lane, spawn_position - cruise_delta * 2.0f);
		STORE_INDEXED_VEC3(soa, position_old_dupe, lane, spawn_position - cruise_delta);
		soa.energy[lane] = soa.calced_max_energy[lane];
		soa.level_start_time[lane] = static_cast<uint64_t>(tick);
		soa.machine_state[lane] &= ~(MACHINESTATE::ZEROHP | MACHINESTATE::FALLOUT | MACHINESTATE::TOOKDAMAGE | MACHINESTATE::LOWGRIP);
		soa.machine_state[lane] |= MACHINESTATE::ACTIVE;
		soa.frames_since_start_2[lane] = 91;
		soa.speed_kmh[lane] = 850.0f;
		soa.base_speed[lane] = 850.0f / 216.0f;
		soa.boost_frames[lane] = 0;
		soa.boost_frames_manual[lane] = 0;
		soa.s_boost_active[lane] = false;
		soa.height_above_track[lane] = 19.5f;
		state.active = 1;
		state.spawn_lap = scheduler_lap;
		state.next_sequence = spawn_sequence;
		bumper_next_sequence = spawn_sequence + 1u;
		break;
	}
}

void GameSim::update_bumper_vehicles()
{
	if (!bumpers_enabled || bumper_count <= 0 || !bumper_cars) {
		return;
	}
	VehicleTickSoA& soa = vehicle_tick_soa;
	PhysicsCarSoA& first_shard = *bumper_cars[0].soa;
	PhysicsCarSoA* bumper_shards = first_shard.shards ? first_shard.shards : &first_shard;
	const int bumper_shard_count = first_shard.shards ? first_shard.shard_count : 1;
	const int sim_lane_count = first_shard.total_lane_count > 0 ? first_shard.total_lane_count : first_shard.lane_count;
	const bool parallel_vehicle_shards = bumper_count >= 16 && bumper_shard_count == VEHICLE_WORKER_COUNT;
	ensure_vehicle_tick_soa_capacity(sim_lane_count);
	for (int i = 0; i < bumper_count; ++i) {
		soa.inputs[i] = generate_bumper_input_for_slot(i);
	}
	for (int i = bumper_count; i < sim_lane_count; ++i) {
		soa.inputs[i] = PlayerInput::from_neutral();
		soa.pending_s_boost_sparks[i] = 0;
	}
	run_vehicle_lanes(bumper_shard_count, parallel_vehicle_shards, [&](int lane, VehicleLaneGroup& group) {
		PhysicsCarSoA& car_soa = bumper_shards[lane];
		const int global_start = car_soa.global_start;
		TrackQueryScratch &track_scratch = vehicle_lane_track_scratch[lane];
		track_scratch.reset_mesh_query();

		begin_vehicle_tick_soa(car_soa, bumper_cars + global_start,
			soa.inputs + global_start, static_cast<uint32_t>(tick), car_soa.count,
			false, false);
		group.sync();

		begin_vehicle_motion_phased_soa(car_soa, bumper_cars + global_start,
			soa.inputs + global_start, car_soa.count, track_scratch);
		group.sync();

		if (lane == 0) {
			commit_vehicle_trigger_events(bumper_shards, bumper_cars, bumper_shard_count, vehicle_lane_track_scratch);
		}
		group.sync();

		finish_vehicle_motion_phased_soa(car_soa, bumper_cars + global_start, car_soa.count);
		group.sync();

		finish_vehicle_tick_soa(car_soa, car_soa.count);
		group.sync();

		post_vehicle_tick_soa(car_soa, bumper_cars + global_start,
			soa.pending_s_boost_sparks + global_start, car_soa.count, false, track_scratch);
	});
}

void GameSim::collide_racers_with_bumpers()
{
	if (!bumpers_enabled || bumper_count <= 0 || !bumper_cars || !cars) {
		return;
	}
	for (int slot = 0; slot < bumper_count; ++slot) {
		if (!bumper_states[slot].active) {
			continue;
		}
		PhysicsCar& bumper = bumper_cars[slot];
		PhysicsCarSoA& bumper_soa = *bumper.soa;
		const int bumper_lane = bumper.soa_index;
		bumper_soa.position_collision_snapshot_x[bumper_lane] = bumper_soa.position_current_x[bumper_lane];
		bumper_soa.position_collision_snapshot_y[bumper_lane] = bumper_soa.position_current_y[bumper_lane];
		bumper_soa.position_collision_snapshot_z[bumper_lane] = bumper_soa.position_current_z[bumper_lane];
		for (int racer_index = 0; racer_index < num_cars; ++racer_index) {
			cars[racer_index].handle_machine_v_bumper_collision(bumper);
		}
	}
}

void GameSim::save_bumper_states_to_saved_state(SavedState& state) const
{
	const int capacity = BUMPER_POOL_SIZE;
	const int count = std::min(bumper_count, capacity);
	state.bumper_state_count = count;
	state.bumper_scheduler_lap = bumper_scheduler_lap;
	state.bumper_next_sequence = bumper_next_sequence;
	for (int i = 0; i < count; ++i) {
		state.bumper_states[i] = bumper_states[i];
	}
}

void GameSim::update_saved_voice_transforms(SavedState& state) const
{
	state.voice_transform_count = 0;
	if (!cars || !car_player_ids || num_cars <= 0) {
		return;
	}
	if (static_cast<int>(state.voice_transforms.size()) < num_cars) {
		state.voice_transforms.resize(num_cars);
	}
	for (int i = 0; i < num_cars; ++i) {
		const int32_t player_id = car_player_ids[i];
		if (player_id < 0) {
			continue;
		}
		const PhysicsCarSoA& soa = *cars[i].soa;
		const int lane = cars[i].soa_index;
		SavedVoiceTransform& dst = state.voice_transforms[state.voice_transform_count++];
		dst.player_id = player_id;
		dst.transform = SimTransform();
		dst.transform.origin = LOAD_INDEXED_VEC3(soa, position_current, lane);
	}
}

void GameSim::restore_bumper_states_from_saved_state(const SavedState& state)
{
	const int capacity = BUMPER_POOL_SIZE;
	const int count = std::min(std::max(state.bumper_state_count, 0), std::min(bumper_count, capacity));
	bumper_scheduler_lap = state.bumper_scheduler_lap;
	bumper_next_sequence = state.bumper_next_sequence;
	for (int i = 0; i < count; ++i) {
		bumper_states[i] = state.bumper_states[i];
	}
	for (int i = count; i < bumper_count && i < BUMPER_POOL_SIZE; ++i) {
		bumper_states[i].active = 0;
		bumper_states[i].spawn_lap = 0;
		bumper_states[i].next_sequence = static_cast<uint32_t>(i);
		bumper_states[i].target_lane = 0.0f;
	}
	for (int i = 0; i < bumper_count && i < BUMPER_POOL_SIZE; ++i) {
		if (!bumper_cars) {
			continue;
		}
		PhysicsCarSoA& soa = *bumper_cars[i].soa;
		const int lane = bumper_cars[i].soa_index;
		if (bumper_states[i].active) {
			soa.current_track[lane] = current_track;
			soa.restore_state[lane] = 0;
			soa.restore_wait_frames[lane] = 0;
			soa.restore_move_frames[lane] = 0;
		} else {
			deactivate_bumper_car(i);
		}
	}
}

PlayerInput GameSim::generate_bumper_input_for_slot(int bumper_slot) const
{
	PlayerInput input = PlayerInput::from_neutral();
	if (!bumper_cars || bumper_slot < 0 || bumper_slot >= bumper_count || !bumper_states[bumper_slot].active) {
		return input;
	}
	const float target_lane = bumper_states[bumper_slot].target_lane;
	const PhysicsCarSoA& soa = *bumper_cars[bumper_slot].soa;
	const int lane = bumper_cars[bumper_slot].soa_index;
	input.accelerate = 1.0f;
	input.boost = false;
	const SimBasis physical_basis = MXT_LOAD_TRANSFORM(soa, basis_physical, lane).basis;
	SimBasis surface = soa.road_sample[lane].closest_surface.basis;
	float road_tx = soa.road_sample[lane].road_t.x;
	if (soa.current_track[lane]) {
		int sample_cp = soa.current_collision_checkpoint[lane];
		if (sample_cp < 0 || sample_cp >= soa.current_track[lane]->num_checkpoints) {
			sample_cp = soa.current_checkpoint[lane];
		}
		if (sample_cp >= 0 && sample_cp < soa.current_track[lane]->num_checkpoints) {
			const CollisionCheckpoint& cp = soa.current_track[lane]->checkpoints[sample_cp];
			const SimVec3 pos = LOAD_INDEXED_VEC3(soa, position_current, lane);
			const SimVec3 p1 = cp.start_plane.project(pos);
			const SimVec3 p2 = cp.end_plane.project(pos);
			const SimVec3 span = p2 - p1;
			const float span_len2 = span.length_squared();
			float cp_t = 0.0f;
			if (span_len2 > 1.0e-6f) {
				cp_t = (pos - p1).dot(span) / span_len2;
				cp_t = std::max(0.0f, std::min(1.0f, cp_t));
			}
			surface[0] = cp.orientation_start[0].lerp(cp.orientation_end[0], cp_t);
			surface[1] = cp.orientation_start[1].lerp(cp.orientation_end[1], cp_t);
			surface[2] = cp.orientation_start[2].lerp(cp.orientation_end[2], cp_t);
			const SimVec3 center = cp.position_start.lerp(cp.position_end, cp_t);
			const float x_radius_inv = lerp(cp.x_radius_start_inv, cp.x_radius_end_inv, cp_t);
			road_tx = (pos - center).dot(surface[0]) * x_radius_inv;
		}
	}
	if (road_tx == -1000.0f || !std::isfinite(road_tx)) {
		road_tx = 0.0f;
	}
	const float bumper_weight = std::max(soa.stat_weight[lane], 0.001f);
	const SimVec3 velocity_world = LOAD_INDEXED_VEC3(soa, velocity, lane) / bumper_weight;
	const float lateral_velocity = velocity_world.dot(surface.c0);
	const float lane_error = std::clamp(road_tx - target_lane, -1.0f, 1.0f);
	float desired_strafe = 0.0f;
	if (std::abs(lane_error) > 0.025f) {
		desired_strafe = lane_error * 1.35f + lateral_velocity * 0.04f;
		desired_strafe = std::clamp(desired_strafe, -0.55f, 0.55f);
	}
	input.strafe_left = std::clamp(-desired_strafe, 0.0f, 1.0f);
	input.strafe_right = std::clamp(desired_strafe, 0.0f, 1.0f);
	const float desired_steer = (physical_basis.c0 + surface.c0).dot(surface.c2);
	input.steer_horizontal = std::clamp(desired_steer * 18.0f, -1.0f, 1.0f);
	if (soa.speed_kmh[lane] > 850.0f) {
		input.brake = std::clamp((soa.speed_kmh[lane] - 850.0f) / 160.0f, 0.0f, 1.0f);
		input.accelerate = 0.0f;
	}
	return input;
}

void GameSim::free_vehicle_tick_soa()
{
	if (vehicle_tick_soa.inputs) {
		free_cache_aligned(vehicle_tick_soa.inputs);
		vehicle_tick_soa.inputs = nullptr;
	}
	if (vehicle_tick_soa.pre_distances) {
		free_cache_aligned(vehicle_tick_soa.pre_distances);
		vehicle_tick_soa.pre_distances = nullptr;
	}
	if (vehicle_tick_soa.placement_distances) {
		free_cache_aligned(vehicle_tick_soa.placement_distances);
		vehicle_tick_soa.placement_distances = nullptr;
	}
	if (vehicle_tick_soa.placement_indices) {
		free_cache_aligned(vehicle_tick_soa.placement_indices);
		vehicle_tick_soa.placement_indices = nullptr;
	}
	vehicle_tick_soa.placement_order_valid = false;
	if (vehicle_tick_soa.pending_s_boost_sparks) {
		free_cache_aligned(vehicle_tick_soa.pending_s_boost_sparks);
		vehicle_tick_soa.pending_s_boost_sparks = nullptr;
	}
	if (vehicle_tick_soa.collision_indices) {
		free_cache_aligned(vehicle_tick_soa.collision_indices);
		vehicle_tick_soa.collision_indices = nullptr;
	}
	if (vehicle_tick_soa.collision_min_x) {
		free_cache_aligned(vehicle_tick_soa.collision_min_x);
		vehicle_tick_soa.collision_min_x = nullptr;
	}
	if (vehicle_tick_soa.collision_max_x) {
		free_cache_aligned(vehicle_tick_soa.collision_max_x);
		vehicle_tick_soa.collision_max_x = nullptr;
	}
	if (vehicle_tick_soa.collision_min_y) {
		free_cache_aligned(vehicle_tick_soa.collision_min_y);
		vehicle_tick_soa.collision_min_y = nullptr;
	}
	if (vehicle_tick_soa.collision_max_y) {
		free_cache_aligned(vehicle_tick_soa.collision_max_y);
		vehicle_tick_soa.collision_max_y = nullptr;
	}
	if (vehicle_tick_soa.collision_min_z) {
		free_cache_aligned(vehicle_tick_soa.collision_min_z);
		vehicle_tick_soa.collision_min_z = nullptr;
	}
	if (vehicle_tick_soa.collision_max_z) {
		free_cache_aligned(vehicle_tick_soa.collision_max_z);
		vehicle_tick_soa.collision_max_z = nullptr;
	}
	vehicle_tick_soa.capacity = 0;
}

void GameSim::ensure_vehicle_tick_soa_capacity(int capacity)
{
	if (capacity <= vehicle_tick_soa.capacity) {
		return;
	}

	free_vehicle_tick_soa();
	vehicle_tick_soa.inputs = static_cast<PlayerInput*>(alloc_cache_aligned(sizeof(PlayerInput) * capacity));
	vehicle_tick_soa.pre_distances = static_cast<float*>(alloc_cache_aligned(sizeof(float) * capacity));
	vehicle_tick_soa.placement_distances = static_cast<float*>(alloc_cache_aligned(sizeof(float) * capacity));
	vehicle_tick_soa.placement_indices = static_cast<int*>(alloc_cache_aligned(sizeof(int) * capacity));
	vehicle_tick_soa.pending_s_boost_sparks = static_cast<uint8_t*>(alloc_cache_aligned(sizeof(uint8_t) * capacity));
	vehicle_tick_soa.collision_indices = static_cast<int*>(alloc_cache_aligned(sizeof(int) * capacity));
	vehicle_tick_soa.collision_min_x = static_cast<float*>(alloc_cache_aligned(sizeof(float) * capacity));
	vehicle_tick_soa.collision_max_x = static_cast<float*>(alloc_cache_aligned(sizeof(float) * capacity));
	vehicle_tick_soa.collision_min_y = static_cast<float*>(alloc_cache_aligned(sizeof(float) * capacity));
	vehicle_tick_soa.collision_max_y = static_cast<float*>(alloc_cache_aligned(sizeof(float) * capacity));
	vehicle_tick_soa.collision_min_z = static_cast<float*>(alloc_cache_aligned(sizeof(float) * capacity));
	vehicle_tick_soa.collision_max_z = static_cast<float*>(alloc_cache_aligned(sizeof(float) * capacity));
	if (!vehicle_tick_soa.inputs || !vehicle_tick_soa.pre_distances ||
		!vehicle_tick_soa.placement_distances || !vehicle_tick_soa.placement_indices ||
		!vehicle_tick_soa.pending_s_boost_sparks || !vehicle_tick_soa.collision_indices ||
		!vehicle_tick_soa.collision_min_x || !vehicle_tick_soa.collision_max_x ||
		!vehicle_tick_soa.collision_min_y || !vehicle_tick_soa.collision_max_y ||
		!vehicle_tick_soa.collision_min_z || !vehicle_tick_soa.collision_max_z) {
		free_vehicle_tick_soa();
		std::abort();
	}
	vehicle_tick_soa.capacity = capacity;
}

void GameSim::set_sim_started(const bool p_sim_started)
{
	sim_started = p_sim_started;
}

bool GameSim::get_sim_started()
{
	return sim_started;
}

String GameSim::get_phase_profile_string() const
{
	return "MXT_PHASE_PROFILE_DISABLED";
}

uint64_t GameSim::render_profile_now_us() const
{
	return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
		std::chrono::steady_clock::now().time_since_epoch()).count());
}

String GameSim::get_render_profile_string() const
{
	if (!render_profile_enabled || render_profile_frames == 0) {
		return "MXT_RENDER_PROFILE_DISABLED";
	}
	auto avg = [](uint64_t total, uint64_t frames) -> godot::String {
		if (frames == 0) {
			return "0";
		}
		return godot::String::num_int64(static_cast<int64_t>(total / frames));
	};
	godot::String out = "MXT_RENDER_PROFILE_CPP frames=" + godot::String::num_int64(static_cast<int64_t>(render_profile_frames));
	out += " total_us=" + avg(render_profile_total_us, render_profile_frames);
	out += " get_children_us=" + avg(render_profile_get_children_us, render_profile_frames);
	out += " cache_us=" + avg(render_profile_cache_us, render_profile_frames);
	out += " snapshots_us=" + avg(render_profile_snapshots_us, render_profile_frames);
	out += " effects_us=" + avg(render_profile_effects_us, render_profile_frames);
	out += " multimesh_us=" + avg(render_profile_multimesh_us, render_profile_frames);
	out += " body_instances=" + avg(render_profile_body_instances, render_profile_frames);
	out += " thruster_instances=" + avg(render_profile_thruster_instances, render_profile_frames);
	out += " camera_us=" + avg(render_profile_camera_us, render_profile_frames);
	out += " local_visual_us=" + avg(render_profile_local_visual_us, render_profile_frames);
	out += " cpu_driver_us=" + avg(render_profile_cpu_driver_us, render_profile_frames);
	out += " spark_us=" + avg(render_profile_spark_us, render_profile_frames);
	out += " visuals_only_frames=" + godot::String::num_int64(static_cast<int64_t>(render_profile_visuals_only_frames));
	out += " visuals_only_total_us=" + avg(render_profile_visuals_only_total_us, render_profile_visuals_only_frames);
	out += " visuals_only_effects_us=" + avg(render_profile_visuals_only_effects_us, render_profile_visuals_only_frames);
	out += " visuals_only_multimesh_us=" + avg(render_profile_visuals_only_multimesh_us, render_profile_visuals_only_frames);
	out += " visuals_only_body_instances=" + avg(render_profile_visuals_only_body_instances, render_profile_visuals_only_frames);
	out += " visuals_only_thruster_instances=" + avg(render_profile_visuals_only_thruster_instances, render_profile_visuals_only_frames);
	out += " visuals_only_camera_us=" + avg(render_profile_visuals_only_camera_us, render_profile_visuals_only_frames);
	return out;
}

void GameSim::set_render_profile_enabled(bool enabled)
{
	render_profile_enabled = enabled;
	render_profile_frames = 0;
	render_profile_total_us = 0;
	render_profile_get_children_us = 0;
	render_profile_cache_us = 0;
	render_profile_snapshots_us = 0;
	render_profile_effects_us = 0;
	render_profile_multimesh_us = 0;
	render_profile_body_instances = 0;
	render_profile_thruster_instances = 0;
	render_profile_camera_us = 0;
	render_profile_local_visual_us = 0;
	render_profile_cpu_driver_us = 0;
	render_profile_spark_us = 0;
	render_profile_visuals_only_frames = 0;
	render_profile_visuals_only_total_us = 0;
	render_profile_visuals_only_effects_us = 0;
	render_profile_visuals_only_multimesh_us = 0;
	render_profile_visuals_only_body_instances = 0;
	render_profile_visuals_only_thruster_instances = 0;
	render_profile_visuals_only_camera_us = 0;
}

void GameSim::set_render_node_effects_enabled(bool enabled)
{
	render_node_effects_enabled = enabled;
	if (!enabled) {
		for (RenderEffectPoolSlot& slot : render_effect_pool_slots) {
			if (slot.recharge_particles) {
				slot.recharge_particles->set_emitting(false);
			}
			if (slot.attack_particles) {
				slot.attack_particles->set_emitting(false);
			}
			if (slot.landing_particles) {
				slot.landing_particles->set_emitting(false);
			}
			if (slot.damage_electricity) {
				slot.damage_electricity->set_visible(false);
				slot.damage_electricity->set_emitting(false);
				slot.damage_electricity->set_amount_ratio(0.0);
			}
			if (slot.damage_smoke) {
				slot.damage_smoke->set_visible(false);
				slot.damage_smoke->set_emitting(false);
				slot.damage_smoke->set_amount_ratio(0.0);
			}
			if (slot.boost_electricity) {
				slot.boost_electricity->set("boosting", false);
				slot.boost_electricity->set("visible", false);
			}
			slot.car_index = -1;
		}
	}
}

void GameSim::set_render_thruster_lights_enabled(bool enabled)
{
	render_thruster_lights_enabled = enabled;
	if (!enabled) {
		hide_unused_render_thruster_lights(0);
	}
}

void GameSim::set_start_grid_slots(godot::PackedInt32Array p_slots)
{
	start_grid_slots.clear();
	start_grid_slots.reserve(p_slots.size());
	for (int i = 0; i < p_slots.size(); ++i) {
		start_grid_slots.push_back(p_slots[i]);
	}
}

int GameSim::get_player_race_place(int player_id) const
{
	if (!cars || !car_player_ids || num_cars <= 0 ||
			!vehicle_tick_soa.placement_order_valid ||
			!vehicle_tick_soa.placement_indices) {
		return 0;
	}

	int place = 1;
	for (int i = 0; i < num_cars; ++i) {
		const int car_index = vehicle_tick_soa.placement_indices[i];
		if (car_index < 0 || car_index >= num_cars) {
			continue;
		}
		if (car_player_ids[car_index] < 0) {
			continue;
		}
		if (car_player_ids[car_index] == player_id) {
			return place;
		}
		place += 1;
	}
	return 0;
}

godot::PackedInt32Array GameSim::get_race_leaderboard_window(int player_id, int max_entries) const
{
	godot::PackedInt32Array window;
	if (!cars || !car_player_ids || num_cars <= 0 ||
			!vehicle_tick_soa.placement_order_valid ||
			!vehicle_tick_soa.placement_indices) {
		return window;
	}
	std::vector<int> ranked_cars;
	ranked_cars.reserve(num_cars);
	for (int i = 0; i < num_cars; ++i) {
		const int car_index = vehicle_tick_soa.placement_indices[i];
		if (car_index >= 0 && car_index < num_cars && car_player_ids[car_index] >= 0) {
			ranked_cars.push_back(car_index);
		}
	}
	if (ranked_cars.empty()) {
		return window;
	}
	const int entry_count = std::max(1, std::min(max_entries, static_cast<int>(ranked_cars.size())));
	int focus_rank = -1;
	for (int i = 0; i < static_cast<int>(ranked_cars.size()); ++i) {
		const int car_index = ranked_cars[i];
		if (car_player_ids[car_index] == player_id) {
			focus_rank = i;
			break;
		}
	}
	if (focus_rank < 0) {
		focus_rank = 0;
	}

	const int half = entry_count >> 1;
	int start = focus_rank - half;
	const int max_start = static_cast<int>(ranked_cars.size()) - entry_count;
	if (start < 0) {
		start = 0;
	}
	if (start > max_start) {
		start = max_start;
	}

	window.resize(1 + entry_count * 2);
	window.set(0, focus_rank + 1);
	int out_index = 1;
	for (int i = 0; i < entry_count; ++i) {
		const int rank = start + i;
		const int car_index = ranked_cars[rank];
		if (car_index < 0 || car_index >= num_cars) {
			window.set(out_index++, -1);
			window.set(out_index++, rank + 1);
			continue;
		}
		window.set(out_index++, car_player_ids[car_index]);
		window.set(out_index++, rank + 1);
	}
	return window;
}

bool GameSim::is_player_race_finished(int player_id) const
{
	if (!cars || !car_player_ids || num_cars <= 0) {
		return false;
	}
	for (int i = 0; i < num_cars; ++i) {
		if (car_player_ids[i] != player_id) {
			continue;
		}
		const PhysicsCarSoA& car_soa = *cars[i].soa;
		const int lane = cars[i].soa_index;
		return (car_soa.machine_state[lane] & MACHINESTATE::COMPLETEDRACE_1_Q) != 0u;
	}
	return false;
}

bool GameSim::is_player_race_eliminated(int player_id) const
{
	if (vehicle_restore_enabled || !cars || !car_player_ids || num_cars <= 0) {
		return false;
	}
	for (int i = 0; i < num_cars; ++i) {
		if (car_player_ids[i] != player_id) {
			continue;
		}
		const PhysicsCarSoA& car_soa = *cars[i].soa;
		const int lane = cars[i].soa_index;
		return vehicle_restore_off_eliminated(car_soa, lane);
	}
	return false;
}

double GameSim::get_player_ko_energy_bonus(int player_id) const
{
	if (!cars || !car_player_ids || num_cars <= 0) {
		return 0.0;
	}
	for (int i = 0; i < num_cars; ++i) {
		if (car_player_ids[i] != player_id) {
			continue;
		}
		const PhysicsCarSoA& car_soa = *cars[i].soa;
		const int lane = cars[i].soa_index;
		return static_cast<double>(car_soa.ko_energy_bonus[lane]);
	}
	return 0.0;
}

void GameSim::set_player_ko_energy_bonus(int player_id, double bonus)
{
	if (!cars || !car_player_ids || num_cars <= 0) {
		return;
	}
	const float clamped_bonus = std::max(0.0f, static_cast<float>(bonus));
	for (int i = 0; i < num_cars; ++i) {
		if (car_player_ids[i] != player_id) {
			continue;
		}
		PhysicsCarSoA& car_soa = *cars[i].soa;
		const int lane = cars[i].soa_index;
		car_soa.ko_energy_bonus[lane] = clamped_bonus;
		if (car_soa.car_properties[lane]) {
			car_soa.calced_max_energy[lane] =
				car_soa.car_properties[lane]->max_energy + car_soa.ko_energy_bonus[lane];
		}
		car_soa.energy[lane] = car_soa.calced_max_energy[lane];
		return;
	}
}

double GameSim::get_player_lap_distance(int player_id) const
{
	if (!cars || !car_player_ids || num_cars <= 0) {
		return 0.0;
	}
	for (int i = 0; i < num_cars; ++i) {
		if (car_player_ids[i] != player_id) {
			continue;
		}
		return static_cast<double>(compute_car_distance_along_track(cars[i]));
	}
	return 0.0;
}

int GameSim::get_player_lap(int player_id) const
{
	if (!cars || !car_player_ids || num_cars <= 0) {
		return 0;
	}
	for (int i = 0; i < num_cars; ++i) {
		if (car_player_ids[i] != player_id) {
			continue;
		}
		const PhysicsCarSoA& car_soa = *cars[i].soa;
		const int lane = cars[i].soa_index;
		return static_cast<int>(car_soa.lap[lane]);
	}
	return 0;
}

int GameSim::get_player_level_start_time(int player_id) const
{
	if (!cars || !car_player_ids || num_cars <= 0) {
		return 300;
	}
	for (int i = 0; i < num_cars; ++i) {
		if (car_player_ids[i] != player_id) {
			continue;
		}
		const PhysicsCarSoA& car_soa = *cars[i].soa;
		const int lane = cars[i].soa_index;
		return static_cast<int>(std::min<uint64_t>(car_soa.level_start_time[lane], static_cast<uint64_t>(INT32_MAX)));
	}
	return 300;
}

godot::String GameSim::get_player_debug_string(int player_id) const
{
	if (!cars || !car_player_ids || num_cars <= 0) {
		return "missing cars";
	}
	for (int i = 0; i < num_cars; ++i) {
		if (car_player_ids[i] != player_id) {
			continue;
		}
		const PhysicsCarSoA& car_soa = *cars[i].soa;
		const int lane = cars[i].soa_index;
		const SimVec3 pos = LOAD_INDEXED_VEC3(car_soa, position_current, lane);
		const SimVec3 vel = LOAD_INDEXED_VEC3(car_soa, velocity, lane);
		const SimVec3 track_normal = LOAD_INDEXED_VEC3(car_soa, track_surface_normal, lane);
		const SimVec3 up = MXT_LOAD_TRANSFORM(car_soa, basis_physical, lane).basis.get_column(1);
		godot::String out = "id=" + godot::String::num_int64(player_id);
		out += " cp=" + godot::String::num_int64(car_soa.current_checkpoint[lane]);
		out += " frac=" + godot::String::num(car_soa.checkpoint_fraction[lane]);
		out += " lap=" + godot::String::num_int64(car_soa.lap[lane]);
		out += " dist=" + godot::String::num(compute_car_distance_along_track(cars[i]));
		out += " pos=(" + godot::String::num(pos.x) + "," + godot::String::num(pos.y) + "," + godot::String::num(pos.z) + ")";
		out += " vel=(" + godot::String::num(vel.x) + "," + godot::String::num(vel.y) + "," + godot::String::num(vel.z) + ")";
		out += " speed=" + godot::String::num(car_soa.speed_kmh[lane]);
		out += " h=" + godot::String::num(car_soa.height_above_track[lane]);
		out += " n=(" + godot::String::num(track_normal.x) + "," + godot::String::num(track_normal.y) + "," + godot::String::num(track_normal.z) + ")";
		out += " up=(" + godot::String::num(up.x) + "," + godot::String::num(up.y) + "," + godot::String::num(up.z) + ")";
		out += " state=0x" + godot::String::num_int64(car_soa.machine_state[lane], 16);
		out += " terrain=0x" + godot::String::num_int64(car_soa.terrain_state[lane], 16);
		return out;
	}
	return "missing player";
}

godot::String GameSim::get_bumper_debug_string() const
{
	godot::String out = "enabled=" + godot::String(bumpers_enabled ? "1" : "0");
	out += " count=" + godot::String::num_int64(bumper_count);
	out += " start=" + godot::String::num_int64(num_cars);
	if (!cars || !bumper_cars || bumper_count <= 0) {
		out += " active=0";
		return out;
	}
	float lead_distance = 0.0f;
	int leader_lap = 0;
	for (int i = 0; i < num_cars; ++i) {
		const PhysicsCarSoA& car_soa = *cars[i].soa;
		const int lane = cars[i].soa_index;
		const float distance = compute_vehicle_distance_along_track(
			car_soa.current_checkpoint[lane],
			car_soa.checkpoint_fraction[lane],
			car_soa.lap[lane]);
		if (distance > lead_distance) {
			lead_distance = distance;
			leader_lap = static_cast<int>(car_soa.lap[lane]);
		}
	}
	if (num_cars > 0) {
		const PhysicsCarSoA& racer_soa = *cars[0].soa;
		const int racer_lane = cars[0].soa_index;
		out += " racer0_cp=" + godot::String::num_int64(racer_soa.current_checkpoint[racer_lane]);
		out += " racer0_coll=" + godot::String::num_int64(racer_soa.current_collision_checkpoint[racer_lane]);
		out += " racer0_lap=" + godot::String::num_int64(racer_soa.lap[racer_lane]);
		out += " racer0_state=0x" + godot::String::num_int64(racer_soa.machine_state[racer_lane], 16);
	}
	out += " leader_lap=" + godot::String::num_int64(leader_lap);
	out += " lead_dist=" + godot::String::num(lead_distance);
	int active_count = 0;
	int first_active = -1;
	for (int slot = 0; slot < bumper_count && slot < BUMPER_POOL_SIZE; ++slot) {
		if (bumper_states[slot].active) {
			if (first_active < 0) {
				first_active = slot;
			}
			++active_count;
		}
	}
	out += " active=" + godot::String::num_int64(active_count);
	if (first_active < 0) {
		return out;
	}
	if (first_active < 0 || first_active >= bumper_count) {
		out += " first_oob=1";
		return out;
	}
	const PhysicsCarSoA& car_soa = *bumper_cars[first_active].soa;
	const int lane = bumper_cars[first_active].soa_index;
	const SimVec3 pos = LOAD_INDEXED_VEC3(car_soa, position_current, lane);
	const SimVec3 vel = LOAD_INDEXED_VEC3(car_soa, velocity, lane);
	out += " first_slot=" + godot::String::num_int64(first_active);
	out += " target_lane=" + godot::String::num(bumper_states[first_active].target_lane);
	out += " cp=" + godot::String::num_int64(car_soa.current_checkpoint[lane]);
	out += " coll_cp=" + godot::String::num_int64(car_soa.current_collision_checkpoint[lane]);
	out += " lap=" + godot::String::num_int64(car_soa.lap[lane]);
	out += " dist=" + godot::String::num(compute_car_distance_along_track(bumper_cars[first_active]));
	out += " road_x=" + godot::String::num(car_soa.road_sample[lane].road_t.x);
	out += " speed=" + godot::String::num(car_soa.speed_kmh[lane]);
	out += " base=" + godot::String::num(car_soa.base_speed[lane]);
	out += " restore=" + godot::String::num_int64(car_soa.restore_state[lane]);
	out += " state2=0x" + godot::String::num_int64(car_soa.state_2[lane], 16);
	out += " pos=(" + godot::String::num(pos.x) + "," + godot::String::num(pos.y) + "," + godot::String::num(pos.z) + ")";
	out += " vel=(" + godot::String::num(vel.x) + "," + godot::String::num(vel.y) + "," + godot::String::num(vel.z) + ")";
	out += " state=0x" + godot::String::num_int64(car_soa.machine_state[lane], 16);
	return out;
}

godot::Array GameSim::get_race_order()
{
	godot::Array order;
	if (!cars || !car_player_ids || num_cars <= 0 ||
			!vehicle_tick_soa.placement_order_valid ||
			!vehicle_tick_soa.placement_indices) {
		return order;
	}
	for (int i = 0; i < num_cars; ++i) {
		const int car_index = vehicle_tick_soa.placement_indices[i];
		if (car_index >= 0 && car_index < num_cars && car_player_ids[car_index] >= 0) {
			order.append(car_player_ids[car_index]);
		}
	}
	return order;
}

godot::Transform3D GameSim::get_player_render_transform(int player_id) const
{
	if (!car_player_ids || render_final_current_transforms.empty()) {
		return godot::Transform3D();
	}
	for (int i = 0; i < num_cars && i < static_cast<int>(render_final_current_transforms.size()); ++i) {
		if (car_player_ids[i] != player_id) {
			continue;
		}
		Engine* engine = Engine::get_singleton();
		const float alpha = engine ? static_cast<float>(engine->get_physics_interpolation_fraction()) : 1.0f;
		SimTransform render_transform = interpolate_sim_transform(
			render_final_prev_transforms[i],
			render_final_current_transforms[i],
			alpha);
		return gd_transform(render_transform);
	}
	return godot::Transform3D();
}

godot::Transform3D GameSim::get_car_render_transform(int car_index) const
{
	if (car_index < 0 ||
			car_index >= num_cars ||
			car_index >= static_cast<int>(render_final_current_transforms.size()) ||
			car_index >= static_cast<int>(render_final_prev_transforms.size())) {
		return godot::Transform3D();
	}
	Engine* engine = Engine::get_singleton();
	const float alpha = engine ? static_cast<float>(engine->get_physics_interpolation_fraction()) : 1.0f;
	SimTransform render_transform = interpolate_sim_transform(
		render_final_prev_transforms[car_index],
		render_final_current_transforms[car_index],
		alpha);
	return gd_transform(render_transform);
}

godot::Transform3D GameSim::get_saved_player_voice_transform(int player_id, int target_tick) const
{
	if (target_tick < 0) {
		return godot::Transform3D();
	}
	const int index = target_tick % STATE_BUFFER_LEN;
	const SavedState& state = state_buffer[index];
	if (state.tick != target_tick || state.voice_transform_count <= 0) {
		return godot::Transform3D();
	}
	const int count = std::min(state.voice_transform_count, static_cast<int>(state.voice_transforms.size()));
	for (int i = 0; i < count; ++i) {
		if (state.voice_transforms[i].player_id == player_id) {
			return gd_transform(state.voice_transforms[i].transform);
		}
	}
	return godot::Transform3D();
}

godot::Array GameSim::get_saved_player_voice_transforms(int target_tick) const
{
	godot::Array out;
	if (target_tick < 0) {
		return out;
	}
	const int index = target_tick % STATE_BUFFER_LEN;
	const SavedState& state = state_buffer[index];
	if (state.tick != target_tick || state.voice_transform_count <= 0) {
		return out;
	}
	const int count = std::min(state.voice_transform_count, static_cast<int>(state.voice_transforms.size()));
	for (int i = 0; i < count; ++i) {
		godot::Dictionary item;
		item["player_id"] = state.voice_transforms[i].player_id;
		item["transform"] = gd_transform(state.voice_transforms[i].transform);
		out.append(item);
	}
	return out;
}

godot::Array GameSim::get_check_warning_candidates(int player_id) const
{
	godot::Array out;
	if (!car_player_ids || num_cars <= 1 || render_final_current_transforms.empty()) {
		return out;
	}
	int focus_index = -1;
	for (int i = 0; i < num_cars && i < static_cast<int>(render_final_current_transforms.size()); ++i) {
		if (car_player_ids[i] == player_id) {
			focus_index = i;
			break;
		}
	}
	if (focus_index < 0) {
		return out;
	}

	Engine* engine = Engine::get_singleton();
	const float alpha = engine ? static_cast<float>(engine->get_physics_interpolation_fraction()) : 1.0f;
	const SimTransform focus = interpolate_sim_transform(
		render_final_prev_transforms[focus_index],
		render_final_current_transforms[focus_index],
		alpha);
	const SimVec3 focus_pos = focus.origin;
	SimVec3 check_right = focus.basis.get_column(0);
	SimVec3 check_forward = -focus.basis.get_column(2);
	if (gameplay_camera_node) {
		const godot::Transform3D camera_transform = gameplay_camera_node->get_global_transform();
		check_right = sim_vec3(camera_transform.basis.get_column(0));
		check_forward = -sim_vec3(camera_transform.basis.get_column(2));
	}
	if (check_forward.length_squared() <= 0.0001f || check_right.length_squared() <= 0.0001f) {
		return out;
	}
	check_forward = check_forward.normalized();
	check_right = check_right.normalized();

	struct CheckWarningCandidate {
		float distance = FLT_MAX;
		float lateral = 0.0f;
		SimVec3 intersect;
		float alpha = 0.0f;
		int32_t player_id = -1;
	};
	constexpr int CHECK_WARNING_LIMIT = 6;
	CheckWarningCandidate best[CHECK_WARNING_LIMIT];
	for (int i = 0; i < num_cars && i < static_cast<int>(render_final_current_transforms.size()); ++i) {
		if (i == focus_index) {
			continue;
		}
		const SimTransform other = interpolate_sim_transform(
			render_final_prev_transforms[i],
			render_final_current_transforms[i],
			alpha);
		const SimVec3 delta = other.origin - focus_pos;
		const float signed_dist = delta.dot(check_forward);
		if (signed_dist >= -1.0f || signed_dist < -80.0f) {
			continue;
		}
		const SimVec3 intersect = other.origin - check_forward * signed_dist;
		const float lateral = (intersect - focus_pos).dot(check_right);
		if (std::abs(lateral) > 70.0f) {
			continue;
		}
		const float alpha_value = std::clamp((signed_dist + 80.0f) / 79.0f, 0.0f, 1.0f);
		const float distance = -signed_dist;
		if (distance >= best[CHECK_WARNING_LIMIT - 1].distance) {
			continue;
		}
		int insert_at = CHECK_WARNING_LIMIT - 1;
		while (insert_at > 0 && distance < best[insert_at - 1].distance) {
			best[insert_at] = best[insert_at - 1];
			--insert_at;
		}
		best[insert_at].distance = distance;
		best[insert_at].lateral = lateral;
		best[insert_at].intersect = intersect;
		best[insert_at].alpha = alpha_value;
		best[insert_at].player_id = car_player_ids[i];
	}
	for (int i = 0; i < CHECK_WARNING_LIMIT; ++i) {
		if (best[i].player_id < 0) {
			continue;
		}
		godot::Dictionary entry;
		entry["player_id"] = best[i].player_id;
		entry["lateral"] = best[i].lateral;
		entry["intersect"] = gd_vec3(best[i].intersect);
		entry["alpha"] = best[i].alpha;
		out.append(entry);
	}
	return out;
}

godot::Array GameSim::consume_race_events()
{
	godot::Array out;
	for (const RaceEvent& event : race_events) {
		godot::Dictionary entry;
		entry["type"] = static_cast<int>(event.type);
		entry["actor_id"] = event.actor_id;
		entry["target_id"] = event.target_id;
		entry["tick"] = event.tick;
		entry["value"] = event.value;
		out.append(entry);
	}
	race_events.clear();
	return out;
}

void GameSim::process_pending_ko_events()
{
	if (!cars || !car_player_ids || num_cars <= 0) {
		return;
	}
	for (int victim_index = 0; victim_index < num_cars; ++victim_index) {
		PhysicsCarSoA& victim_soa = *cars[victim_index].soa;
		const int victim_lane = cars[victim_index].soa_index;
		const int attacker_index = victim_soa.pending_ko_attacker_car_index[victim_lane];
		if (attacker_index < 0 || attacker_index >= num_cars || attacker_index == victim_index) {
			victim_soa.pending_ko_attacker_car_index[victim_lane] = -1;
			continue;
		}

		PhysicsCarSoA& attacker_soa = *cars[attacker_index].soa;
		const int attacker_lane = cars[attacker_index].soa_index;
		const float boost_cost = std::max(1.0f,
			10.0f * attacker_soa.stat_boost_length[attacker_lane] * attacker_soa.boost_energy_use_mult[attacker_lane]);
		const float energy_gain = boost_cost * 0.6666666667f;
		if (car_player_ids[attacker_index] >= 0) {
			attacker_soa.ko_energy_bonus[attacker_lane] += energy_gain;
			if (attacker_soa.car_properties[attacker_lane]) {
				attacker_soa.calced_max_energy[attacker_lane] =
					attacker_soa.car_properties[attacker_lane]->max_energy + attacker_soa.ko_energy_bonus[attacker_lane];
			}
			attacker_soa.energy[attacker_lane] = attacker_soa.calced_max_energy[attacker_lane];
			attacker_soa.machine_state[attacker_lane] &= ~(MACHINESTATE::ZEROHP |
				MACHINESTATE::FALLOUT |
				MACHINESTATE::TOOKDAMAGE |
				MACHINESTATE::LOWGRIP);
			attacker_soa.breakdown_frame_counter[attacker_lane] = 0;
			attacker_soa.some_breakdown_int[attacker_lane] = 0;
			attacker_soa.frames_since_death[attacker_lane] = 0;
			attacker_soa.machine_crashed[attacker_lane] = false;
			attacker_soa.state_2[attacker_lane] &= ~(0x2u | 0x20u | 0x80u | 0x100u);
			STORE_INDEXED_VEC3(attacker_soa, visual_rotation, attacker_lane, SimVec3());
			STORE_INDEXED_VEC3(attacker_soa, unk_vec3_0x4e4, attacker_lane, SimVec3());
			STORE_INDEXED_VEC3(attacker_soa, unk_vec3_0x4f0, attacker_lane, SimVec3());
		}

		RaceEvent event;
		event.type = 1;
		event.actor_id = car_player_ids[attacker_index];
		event.target_id = car_player_ids[victim_index];
		event.tick = tick;
		event.value = static_cast<int32_t>(std::lround(energy_gain));
		race_events.push_back(event);
		victim_soa.pending_ko_attacker_car_index[victim_lane] = -1;
	}
	if (!bumpers_enabled || !bumper_cars || bumper_count <= 0) {
		return;
	}
	for (int slot = 0; slot < bumper_count; ++slot) {
		if (!bumper_states[slot].active) {
			continue;
		}
		PhysicsCarSoA& victim_soa = *bumper_cars[slot].soa;
		const int victim_lane = bumper_cars[slot].soa_index;
		const int attacker_index = victim_soa.pending_ko_attacker_car_index[victim_lane];
		if (attacker_index < 0 || attacker_index >= num_cars) {
			victim_soa.pending_ko_attacker_car_index[victim_lane] = -1;
			continue;
		}
		PhysicsCarSoA& attacker_soa = *cars[attacker_index].soa;
		const int attacker_lane = cars[attacker_index].soa_index;
		const float boost_cost = std::max(1.0f,
			10.0f * attacker_soa.stat_boost_length[attacker_lane] * attacker_soa.boost_energy_use_mult[attacker_lane]);
		const float energy_gain = boost_cost * 0.75f;
		if (car_player_ids[attacker_index] >= 0) {
			attacker_soa.ko_energy_bonus[attacker_lane] += energy_gain;
			if (attacker_soa.car_properties[attacker_lane]) {
				attacker_soa.calced_max_energy[attacker_lane] =
					attacker_soa.car_properties[attacker_lane]->max_energy + attacker_soa.ko_energy_bonus[attacker_lane];
			}
			attacker_soa.energy[attacker_lane] = std::min(
				attacker_soa.energy[attacker_lane] + energy_gain,
				attacker_soa.calced_max_energy[attacker_lane]);
		}
		RaceEvent event;
		event.type = 1;
		event.actor_id = car_player_ids[attacker_index];
		event.target_id = -1;
		event.tick = tick;
		event.value = static_cast<int32_t>(std::lround(energy_gain));
		race_events.push_back(event);
		victim_soa.pending_ko_attacker_car_index[victim_lane] = -1;
		deactivate_bumper_car(slot);
	}
}

static inline PlayerInput native_cpu_generate_input_for_car(const PhysicsCar& car, int32_t player_id, int expected_tick, int spawn_seed);

void GameSim::tick_singleplayer(int local_player_id, godot::PackedByteArray local_input)
{
	const PlayerInput decoded_local_input = PlayerInput::from_bytes(local_input);
	tick_gamesim_internal(InputFrameMode::SingleLocal, local_player_id, &decoded_local_input, nullptr, nullptr, 0);
}

void GameSim::tick_gamesim_internal(InputFrameMode mode,
	int local_player_id,
	const PlayerInput* local_input,
	const PlayerInput* decoded_car_inputs,
	const uint8_t* decoded_car_input_present,
	int decoded_car_input_count)
{
	//godot::Object* dd3d = godot::Engine::get_singleton()->get_singleton("DebugDraw3D");

	std::fesetround(FE_TONEAREST);
	std::feclearexcept(FE_ALL_EXCEPT);

	if (num_cars <= 0 || !cars)
	{
		save_state();
		tick += 1;
		return;
	}

	VehicleTickSoA& soa = vehicle_tick_soa;
	PhysicsCarSoA& first_shard = *cars[0].soa;
	PhysicsCarSoA* car_shards = first_shard.shards ? first_shard.shards : &first_shard;
	const int car_shard_count = first_shard.shards ? first_shard.shard_count : 1;
	const int sim_lane_count = first_shard.total_lane_count > 0 ? first_shard.total_lane_count : first_shard.lane_count;
	const bool parallel_vehicle_shards = num_cars >= 16 && car_shard_count == VEHICLE_WORKER_COUNT;
	ensure_vehicle_tick_soa_capacity(sim_lane_count);
	int buf_index = tick % INPUT_BUFFER_LEN;
	PlayerInput* slot = input_buffer + buf_index * num_cars;

	float lead_distance = 0.0f;
	int leader_lap = 0;
	for (int i = 0; i < num_cars; ++i) {
		PhysicsCarSoA& car_soa = *cars[i].soa;
		const int lane = cars[i].soa_index;
		const float distance = compute_vehicle_distance_along_track(
			car_soa.current_checkpoint[lane], car_soa.checkpoint_fraction[lane], car_soa.lap[lane]);
		soa.pre_distances[i] = distance;
		if (distance > lead_distance) {
			lead_distance = distance;
			leader_lap = static_cast<int>(car_soa.lap[lane]);
		}
	}
	update_bumpers(lead_distance, leader_lap);

	for (int i = 0; i < num_cars; i++) {
		PlayerInput inp = PlayerInput::from_neutral();
		const int32_t player_id = car_player_ids ? car_player_ids[i] : -1;
		PhysicsCarSoA& car_soa = *cars[i].soa;
		const int lane = cars[i].soa_index;
		const bool completed_race = (car_soa.machine_state[lane] & MACHINESTATE::COMPLETEDRACE_1_Q) != 0u;
		if (mode == InputFrameMode::DecodedCarArray && i < decoded_car_input_count && decoded_car_inputs &&
				(!decoded_car_input_present || decoded_car_input_present[i])) {
			inp = decoded_car_inputs[i];
		} else if (completed_race && player_id != -1) {
			inp = native_cpu_generate_input_for_car(cars[i], player_id, tick, spawn_seed);
		} else if (mode == InputFrameMode::SingleLocal && player_id == local_player_id && local_input) {
			inp = *local_input;
		} else if (car_is_cpu && car_is_cpu[i]) {
			inp = native_cpu_generate_input_for_car(cars[i], player_id, tick, spawn_seed);
		}
		inp = PlayerInput::quantized(inp);
		if (s_boost_enabled && !car_soa.s_boost_active[lane] && car_soa.s_boost_charge[lane] >= car_soa.s_boost_charge_max[lane] && inp.boost) {
			float gap = lead_distance - soa.pre_distances[i];
			if (gap < 0.0f) {
				gap = 0.0f;
			}
			uint16_t duration_frames = compute_s_boost_duration_frames(gap);
			if (duration_frames == 0)
				duration_frames = 1;
			car_soa.s_boost_active[lane] = true;
			car_soa.s_boost_frames_remaining[lane] = duration_frames;
			car_soa.s_boost_charge[lane] = 0;
			car_soa.s_boost_emit_frame_accumulator[lane] = 0;
			car_soa.s_boost_pending_spark_spawns[lane] = 0;
			car_soa.boost_frames[lane] = 0;
			car_soa.boost_frames_manual[lane] = 0;
			car_soa.boost_turbo[lane] = 0.0f;
			car_soa.dashplate_heat_multiplier[lane] = 1.0f;
			car_soa.boost_delay_frame_counter[lane] = 0;
			car_soa.car_hit_invincibility[lane] = 0;
			car_soa.machine_state[lane] &= ~(MACHINESTATE::JUST_PRESSED_BOOST |
				MACHINESTATE::BOOSTING |
				MACHINESTATE::BOOSTING_DASHPLATE |
				MACHINESTATE::SIDEATTACKING |
				MACHINESTATE::SPINATTACKING |
				MACHINESTATE::TOOKDAMAGE |
				MACHINESTATE::LOWGRIP);
			inp.boost = false;
		}
		soa.inputs[i] = inp;
		slot[i] = inp;
	}
	for (int i = num_cars; i < sim_lane_count; ++i) {
		soa.inputs[i] = PlayerInput::from_neutral();
		soa.pending_s_boost_sparks[i] = 0;
	}

	run_vehicle_lanes(car_shard_count, parallel_vehicle_shards, [&](int lane, VehicleLaneGroup& group) {
		PhysicsCarSoA& car_soa = car_shards[lane];
		const int global_start = car_soa.global_start;
		TrackQueryScratch &track_scratch = vehicle_lane_track_scratch[lane];
		track_scratch.reset_mesh_query();

		begin_vehicle_tick_soa(car_soa, cars + global_start,
			soa.inputs + global_start, static_cast<uint32_t>(tick), car_soa.count,
			vehicle_restore_enabled, s_boost_enabled);
		group.sync();

		begin_vehicle_motion_phased_soa(car_soa, cars + global_start,
			soa.inputs + global_start, car_soa.count, track_scratch);
		group.sync();

		if (lane == 0) {
			commit_vehicle_trigger_events(car_shards, cars, car_shard_count, vehicle_lane_track_scratch);
		}
		group.sync();

		finish_vehicle_motion_phased_soa(car_soa, cars + global_start, car_soa.count);
		group.sync();

		finish_vehicle_tick_soa(car_soa, car_soa.count);
		group.sync();

		if (lane == 0) {
			collide_vehicles_broadphase(*this, cars, num_cars,
				soa.collision_indices,
				soa.collision_min_x, soa.collision_max_x,
				soa.collision_min_y, soa.collision_max_y,
				soa.collision_min_z, soa.collision_max_z);
		}
		group.sync();

		post_vehicle_tick_soa(car_soa, cars + global_start,
			soa.pending_s_boost_sparks + global_start, car_soa.count, s_boost_enabled, track_scratch);
	});
	for (int i = 0; i < num_cars; i++) {
		if (s_boost_enabled && soa.pending_s_boost_sparks[i] > 0) {
			emit_super_sparks_from_car(cars[i], soa.pending_s_boost_sparks[i]);
		}
	}
	update_bumper_vehicles();
	collide_racers_with_bumpers();

	for (int i = 0; i < num_cars; ++i) {
		PhysicsCarSoA& car_soa = *cars[i].soa;
		const int lane = cars[i].soa_index;
		soa.placement_distances[i] = compute_vehicle_distance_along_track(
			car_soa.current_checkpoint[lane], car_soa.checkpoint_fraction[lane], car_soa.lap[lane]);
		soa.placement_indices[i] = i;
	}
	std::sort(soa.placement_indices, soa.placement_indices + num_cars, [&](int a, int b) {
		return soa.placement_distances[a] > soa.placement_distances[b];
	});
	soa.placement_order_valid = true;

	if (s_boost_enabled && super_spark_state) {
		super_spark_state->placement_timer += 1;
		while (super_spark_state->placement_timer >= 120) {
			int top_racer_indices[3] = { -1, -1, -1 };
			int top_racer_count = 0;
			for (int i = 0; i < num_cars && top_racer_count < 3; ++i) {
				const int car_index = soa.placement_indices[i];
				if (car_index >= 0 && car_index < num_cars && car_player_ids && car_player_ids[car_index] >= 0) {
					top_racer_indices[top_racer_count++] = car_index;
				}
			}
			if (top_racer_count > 0) {
				emit_super_sparks_from_car(cars[top_racer_indices[0]], 4);
			}
			if (top_racer_count > 1) {
				emit_super_sparks_from_car(cars[top_racer_indices[1]], 3);
			}
			if (top_racer_count > 2) {
				emit_super_sparks_from_car(cars[top_racer_indices[2]], 2);
			}
			super_spark_state->placement_timer -= 120;
		}
	}

	process_pending_ko_events();
	if (s_boost_enabled) {
		update_super_sparks();
	}

	//for (int i = 0; i < num_cars; i++)
	//{
	//	if (i == 0){
	//		CollisionData collision;
	//		godot::Vector3 p0 = cars[i].position + godot::Vector3(0, 5, 3);
	//		godot::Vector3 p1 = cars[i].position + godot::Vector3(0, -100, 3);
	//		current_track->cast_vs_track(collision, p0, p1, CAST_FLAGS::WANTS_TRACK, cars[i].soa->current_collision_checkpoint[cars[i].soa_index]);
	//		if (collision.collided){
	//			dd3d->call("draw_arrow", collision.collision_point, collision.collision_point + collision.collision_normal * 2, godot::Color(0.0f, 1.0f, 0.0f), 0.25, true, _TICK_DELTA);
	//		}
	//		dd3d->call("draw_arrow", p0, p1, godot::Color(1.0f, 0.0f, 0.0f), 0.25, true, _TICK_DELTA);
	//	}
	//}
	save_state();
	
	
	tick += 1;
	//dd2d->call("set_text", "pos 1", car_positions[0]);
	
	//dd3d->call("draw_points", car_positions, 0, 1.0f, godot::Color(1.f, 0.f, 0.f), 0.0166666);
}

Array GameSim::get_dip_switches() const
{
	Array switches;
	for (const auto& def : DIP_SWITCH_DEFINITIONS) {
		Dictionary entry;
		entry["key"] = String(def.key);
		entry["label"] = String(def.label);
		entry["flag"] = def.flag;
		entry["enabled"] = DEBUG::dip_enabled(def.flag);
		switches.push_back(entry);
	}
	return switches;
}

bool GameSim::is_dip_switch_enabled(int flag) const
{
	return DEBUG::dip_enabled(flag);
}

void GameSim::set_dip_switch_enabled(int flag, bool enabled)
{
	if (enabled) {
		DEBUG::enable_dip(flag);
	} else {
		DEBUG::disable_dip(flag);
	}
}

double GameSim::get_first_lap_distance() const
{
	if (!sim_started || !cars || num_cars <= 0 || !current_track)
	{
		return 0.0;
	}
	return static_cast<double>(compute_car_distance_along_track(cars[0]));
}

double GameSim::get_track_lap_length() const
{
	if (!current_track) {
		return 0.0;
	}
	float lap_length = current_track->lap_length;
	if (lap_length <= 0.0f && current_track->num_checkpoints > 0) {
		lap_length = current_track->checkpoints[current_track->num_checkpoints - 1].distance;
	}
	return static_cast<double>(lap_length);
}

void GameSim::instantiate_gamesim(StreamPeerBuffer* lvldat_buf, godot::Array car_prop_buffers, godot::Array accel_settings)
{
	if (Engine::get_singleton()->is_editor_hint()) return;

	tick = 0;
	race_events.clear();

	int32_t buffer_size = lvldat_buf->get_size();
	const int requested_cars_hint = car_prop_buffers.size() > 0 ? car_prop_buffers.size() : 1;

	const size_t level_heap_size = std::max<size_t>(
		1024u * 1024u * 32u,
		static_cast<size_t>(buffer_size) * 4u + 1024u * 1024u * 8u);
	level_data.instantiate(level_heap_size);

	const int bumper_capacity_hint = bumpers_enabled ? BUMPER_POOL_SIZE : 0;
	gamestate_data.instantiate(1024 * 1024 + static_cast<size_t>(requested_cars_hint + bumper_capacity_hint) * 8192u);
	spark_multimesh_instance = nullptr;
	super_spark_state = gamestate_data.allocate_object<SuperSparkState>();
	if (super_spark_state) {
		super_spark_state->cursor = 0;
		super_spark_state->placement_timer = 0;
		super_spark_state->rng_state = 1;
		super_sparks = super_spark_state->sparks;
		reset_super_sparks();
		super_spark_state->rng_state = static_cast<uint32_t>(spawn_seed) ^ 0xA511E9B1u;
		if (super_spark_state->rng_state == 0) {
			super_spark_state->rng_state = 1;
		}
	} else {
		super_sparks = nullptr;
		spark_multimesh_instance = nullptr;
	}
	int state_capacity = gamestate_data.get_capacity();
	for (int i = 0; i < STATE_BUFFER_LEN; i++)
	{
		state_buffer[i].data = (char*)malloc(state_capacity);
		state_buffer[i].size = 0;
		state_buffer[i].tick = -1;
		state_buffer[i].voice_transform_count = 0;
		state_buffer[i].voice_transforms.resize(requested_cars_hint);
	}

	current_track = level_data.allocate_class<RaceTrack>();
	current_track->num_trigger_colliders = 0;
	current_track->trigger_colliders = nullptr;
	current_track->num_mesh_collision_triangles = 0;
	current_track->mesh_collision_triangles = nullptr;
	current_track->mesh_world_bvh_nodes = nullptr;
	current_track->mesh_world_bvh_triangle_indices = nullptr;
	current_track->num_mesh_world_bvh_nodes = 0;
	current_track->mesh_floor_bvh_nodes = nullptr;
	current_track->mesh_floor_bvh_triangle_indices = nullptr;
	current_track->num_mesh_floor_bvh_nodes = 0;
	current_track->mesh_overlay_bvh_nodes = nullptr;
	current_track->mesh_overlay_bvh_triangle_indices = nullptr;
	current_track->num_mesh_overlay_bvh_nodes = 0;
	current_track->num_mesh_overlay_triangles = 0;
	current_track->lap_length = 0.0f;

	uint32_t header_size = lvldat_buf->get_u32();
	String version_string = lvldat_buf->get_string(4);
	if (version_string != "v0.9") {
		UtilityFunctions::printerr(String("MXT track format hard-cutover failure: expected v0.9, got "), version_string);
		std::abort();
	}
	uint32_t checkpoint_count = lvldat_buf->get_u32();
	uint32_t segment_count = lvldat_buf->get_u32();
	uint32_t trigger_count = lvldat_buf->get_u32();
	uint32_t mesh_collision_triangle_count = lvldat_buf->get_u32();

	std::vector<uint32_t> neighboring_checkpoint_indices;


	//DEBUG::enable_dip(DIP_SWITCH::DIP_DRAW_SEGMENT_SURF);
	//DEBUG::enable_dip(DIP_SWITCH::DIP_DRAW_CHECKPOINTS);
	//DEBUG::enable_dip(DIP_SWITCH::DIP_DRAW_TILT_CORNER_DATA);
	//DEBUG::enable_dip(DIP_SWITCH::DIP_DRAW_SEG_BOUNDS);
	// load in collision checkpoints //

	current_track->num_checkpoints = checkpoint_count;
	current_track->checkpoints = level_data.allocate_array<CollisionCheckpoint>(checkpoint_count);

	for (int i = 0; i < checkpoint_count; i++)
	{
		current_track->checkpoints[i].position_start[0] = lvldat_buf->get_float();
		current_track->checkpoints[i].position_start[1] = lvldat_buf->get_float();
		current_track->checkpoints[i].position_start[2] = lvldat_buf->get_float();
		current_track->checkpoints[i].position_end[0] = lvldat_buf->get_float();
		current_track->checkpoints[i].position_end[1] = lvldat_buf->get_float();
		current_track->checkpoints[i].position_end[2] = lvldat_buf->get_float();
		current_track->checkpoints[i].orientation_start[0][0] = lvldat_buf->get_float();
		current_track->checkpoints[i].orientation_start[0][1] = lvldat_buf->get_float();
		current_track->checkpoints[i].orientation_start[0][2] = lvldat_buf->get_float();
		current_track->checkpoints[i].orientation_start[1][0] = lvldat_buf->get_float();
		current_track->checkpoints[i].orientation_start[1][1] = lvldat_buf->get_float();
		current_track->checkpoints[i].orientation_start[1][2] = lvldat_buf->get_float();
		current_track->checkpoints[i].orientation_start[2][0] = lvldat_buf->get_float();
		current_track->checkpoints[i].orientation_start[2][1] = lvldat_buf->get_float();
		current_track->checkpoints[i].orientation_start[2][2] = lvldat_buf->get_float();
		current_track->checkpoints[i].orientation_end[0][0] = lvldat_buf->get_float();
		current_track->checkpoints[i].orientation_end[0][1] = lvldat_buf->get_float();
		current_track->checkpoints[i].orientation_end[0][2] = lvldat_buf->get_float();
		current_track->checkpoints[i].orientation_end[1][0] = lvldat_buf->get_float();
		current_track->checkpoints[i].orientation_end[1][1] = lvldat_buf->get_float();
		current_track->checkpoints[i].orientation_end[1][2] = lvldat_buf->get_float();
		current_track->checkpoints[i].orientation_end[2][0] = lvldat_buf->get_float();
		current_track->checkpoints[i].orientation_end[2][1] = lvldat_buf->get_float();
		current_track->checkpoints[i].orientation_end[2][2] = lvldat_buf->get_float();
		current_track->checkpoints[i].orientation_start.orthonormalize();
		current_track->checkpoints[i].orientation_end.orthonormalize();
		current_track->checkpoints[i].x_radius_start = lvldat_buf->get_float();
		current_track->checkpoints[i].y_radius_start = lvldat_buf->get_float();
		current_track->checkpoints[i].x_radius_end = lvldat_buf->get_float();
		current_track->checkpoints[i].y_radius_end = lvldat_buf->get_float();
		current_track->checkpoints[i].t_start = lvldat_buf->get_float();
		current_track->checkpoints[i].t_end = lvldat_buf->get_float();
		current_track->checkpoints[i].local_distance = lvldat_buf->get_float();
		current_track->checkpoints[i].distance = 0.0f;
		current_track->checkpoints[i].road_segment = (int)lvldat_buf->get_u32();
		current_track->checkpoints[i].start_plane.normal[0] = lvldat_buf->get_float();
		current_track->checkpoints[i].start_plane.normal[1] = lvldat_buf->get_float();
		current_track->checkpoints[i].start_plane.normal[2] = lvldat_buf->get_float();
		current_track->checkpoints[i].start_plane.d = lvldat_buf->get_float();
		current_track->checkpoints[i].end_plane.normal[0] = lvldat_buf->get_float();
		current_track->checkpoints[i].end_plane.normal[1] = lvldat_buf->get_float();
		current_track->checkpoints[i].end_plane.normal[2] = lvldat_buf->get_float();
		current_track->checkpoints[i].end_plane.d = lvldat_buf->get_float();
		current_track->checkpoints[i].x_radius_start_inv = 1.0f / fmaxf(0.001f, current_track->checkpoints[i].x_radius_start);
		current_track->checkpoints[i].y_radius_start_inv = 1.0f / fmaxf(0.001f, current_track->checkpoints[i].y_radius_start);
		current_track->checkpoints[i].x_radius_end_inv = 1.0f / fmaxf(0.001f, current_track->checkpoints[i].x_radius_end);
		current_track->checkpoints[i].y_radius_end_inv = 1.0f / fmaxf(0.001f, current_track->checkpoints[i].y_radius_end);
		int num_n_cp = (int)lvldat_buf->get_u32();

		current_track->checkpoints[i].num_neighboring_checkpoints = num_n_cp;

		current_track->checkpoints[i].neighboring_checkpoints = level_data.allocate_array<int>(num_n_cp);
		for (int n = 0; n < num_n_cp; n++)
		{
			current_track->checkpoints[i].neighboring_checkpoints[n] = (int)lvldat_buf->get_u32();
		}
	}

	current_track->compute_checkpoint_distances();

	// load in track segments //
	current_track->minimum_y = 0.0f;

	current_track->num_segments = segment_count;
	current_track->segments = level_data.allocate_array<TrackSegment>(segment_count);

	for (int seg = 0; seg < segment_count; seg++)
	{
		int segment_index = (int)lvldat_buf->get_u32();
		int road_type = (int)lvldat_buf->get_u32();
		current_track->segments[seg].analytic_collision_enabled = lvldat_buf->get_u32() != 0;

		// what road shape? //

		if (road_type == 0)
		{
			current_track->segments[seg].road_shape = level_data.allocate_class<RoadShape>();
			current_track->segments[seg].road_shape->shape_type = ROAD_SHAPE_TYPE::ROAD_SHAPE_FLAT;
		}
		else if (road_type == 1)
		{
			current_track->segments[seg].road_shape = level_data.allocate_class<RoadShapeCylinder>();
			current_track->segments[seg].road_shape->shape_type = ROAD_SHAPE_TYPE::ROAD_SHAPE_CYLINDER;
		}
		else if (road_type == 2)
		{
			current_track->segments[seg].road_shape = level_data.allocate_class<RoadShapeCylinderOpen>();
			current_track->segments[seg].road_shape->openness = level_data.allocate_curve_from_buffer(lvldat_buf);
			current_track->segments[seg].road_shape->shape_type = ROAD_SHAPE_TYPE::ROAD_SHAPE_CYLINDER_OPEN;
		}
		else if (road_type == 3)
		{
			current_track->segments[seg].road_shape = level_data.allocate_class<RoadShapePipe>();
			current_track->segments[seg].road_shape->shape_type = ROAD_SHAPE_TYPE::ROAD_SHAPE_PIPE;
		}
		else if (road_type == 4)
		{
			current_track->segments[seg].road_shape = level_data.allocate_class<RoadShapePipeOpen>();
			current_track->segments[seg].road_shape->openness = level_data.allocate_curve_from_buffer(lvldat_buf);
			current_track->segments[seg].road_shape->shape_type = ROAD_SHAPE_TYPE::ROAD_SHAPE_PIPE_OPEN;
		}
		else if (road_type == 5)
		{
			auto* rs = level_data.allocate_class<RoadShapeRoundedRect>();
			rs->width = level_data.allocate_curve_from_buffer(lvldat_buf);
			rs->height = level_data.allocate_curve_from_buffer(lvldat_buf);
			rs->radius = level_data.allocate_curve_from_buffer(lvldat_buf);
			current_track->segments[seg].road_shape = rs;
			current_track->segments[seg].road_shape->shape_type = ROAD_SHAPE_TYPE::ROAD_SHAPE_ROUNDED_RECT;
		}
		else if (road_type == 6)
		{
			auto* rs = level_data.allocate_class<RoadShapeRoundedRectOpen>();
			rs->width = level_data.allocate_curve_from_buffer(lvldat_buf);
			rs->height = level_data.allocate_curve_from_buffer(lvldat_buf);
			rs->radius = level_data.allocate_curve_from_buffer(lvldat_buf);
			rs->openness = level_data.allocate_curve_from_buffer(lvldat_buf);
			rs->open_rotation = level_data.allocate_curve_from_buffer(lvldat_buf);
			current_track->segments[seg].road_shape = rs;
			current_track->segments[seg].road_shape->shape_type = ROAD_SHAPE_TYPE::ROAD_SHAPE_ROUNDED_RECT_OPEN;
		}
		else if (road_type == 7)
		{
			current_track->segments[seg].road_shape = level_data.allocate_class<RoadShape>();
			current_track->segments[seg].road_shape->shape_type = ROAD_SHAPE_TYPE::ROAD_SHAPE_TUNNEL;
		}

		// road modulations //

		int modulation_count = (int)lvldat_buf->get_u32();
		current_track->segments[seg].road_shape->num_modulations = modulation_count;

		if (modulation_count > 0)
		{
			current_track->segments[seg].road_shape->road_modulations = level_data.allocate_array<RoadModulation>(modulation_count);
			for (int mod = 0; mod < modulation_count; mod++)
			{
				current_track->segments[seg].road_shape->road_modulations[mod].modulation_effect = level_data.allocate_curve_from_buffer(lvldat_buf);
				current_track->segments[seg].road_shape->road_modulations[mod].modulation_height = level_data.allocate_curve_from_buffer(lvldat_buf);
			}
		}

		// road embeds //

		int embed_count = (int)lvldat_buf->get_u32();
		current_track->segments[seg].road_shape->num_embeds = embed_count;
		if (embed_count > 0)
		{
			current_track->segments[seg].road_shape->road_embeds = level_data.allocate_array<RoadEmbed>(embed_count);
			for (int embed = 0; embed < embed_count; embed++)
			{
				current_track->segments[seg].road_shape->road_embeds[embed].start_offset = lvldat_buf->get_float();
				current_track->segments[seg].road_shape->road_embeds[embed].end_offset = lvldat_buf->get_float();
				int desired_embed = (int)lvldat_buf->get_u32();
				if (desired_embed == EMBED_TYPE_TO_TERRAIN::RECHARGE){
					current_track->segments[seg].road_shape->road_embeds[embed].embed_type = TERRAIN::RECHARGE;
				}
				if (desired_embed == EMBED_TYPE_TO_TERRAIN::DIRT){
					current_track->segments[seg].road_shape->road_embeds[embed].embed_type = TERRAIN::DIRT;
				}
				if (desired_embed == EMBED_TYPE_TO_TERRAIN::ICE){
					current_track->segments[seg].road_shape->road_embeds[embed].embed_type = TERRAIN::ICE;
				}
				if (desired_embed == EMBED_TYPE_TO_TERRAIN::LAVA){
					current_track->segments[seg].road_shape->road_embeds[embed].embed_type = TERRAIN::LAVA;
				}
				if (desired_embed == EMBED_TYPE_TO_TERRAIN::HOLE){
					current_track->segments[seg].road_shape->road_embeds[embed].embed_type = TERRAIN::HOLE;
				}
				
				current_track->segments[seg].road_shape->road_embeds[embed].left_border = level_data.allocate_curve_from_buffer(lvldat_buf);
				current_track->segments[seg].road_shape->road_embeds[embed].right_border = level_data.allocate_curve_from_buffer(lvldat_buf);
			}
		}

		current_track->segments[seg].road_shape->owning_segment = &current_track->segments[seg];

		int pos = lvldat_buf->get_position();
		int num_keyframes = static_cast<int>(lvldat_buf->get_u32());
		lvldat_buf->seek(pos);
      // 1) allocate the SoA object itself on your heap
		{
			uintptr_t addr = reinterpret_cast<uintptr_t>(level_data.heap_allocation);
			uintptr_t mis = addr & 31;
			if (mis) {
				level_data.allocate_bytes(32 - mis);
			}
		}
		void *raw = level_data.allocate_bytes(sizeof(RoadTransformCurve));
		RoadTransformCurve *soa = new (raw) RoadTransformCurve(num_keyframes);
		current_track->segments[seg].curve_matrix = soa;

		auto align32 = [&]() {
			uintptr_t addr = reinterpret_cast<uintptr_t>(level_data.heap_allocation);
			uintptr_t mis = addr & 31;
			if (mis) {
				level_data.allocate_bytes(32 - mis);
			}
		};

		// 2) allocate each float array, after aligning
		align32();
		soa->times       = level_data.allocate_array<float>(num_keyframes);

		align32();
		soa->values      = level_data.allocate_array<float>(num_keyframes * 16);

		align32();
		soa->tangent_in  = level_data.allocate_array<float>(num_keyframes * 16);

		align32();
		soa->tangent_out = level_data.allocate_array<float>(num_keyframes * 16);

		int seg_count = num_keyframes > 0 ? num_keyframes - 1 : 0;

		align32();
		soa->inv_dt  = level_data.allocate_array<float>(seg_count);
		align32();
		soa->coef_a  = level_data.allocate_array<float>(seg_count * 16);
		align32();
		soa->coef_b  = level_data.allocate_array<float>(seg_count * 16);
		align32();
		soa->coef_c  = level_data.allocate_array<float>(seg_count * 16);
		align32();
		soa->coef_d  = level_data.allocate_array<float>(seg_count * 16);

		// 3) fill your keyframes
		for (int n = 0; n < 15; ++n) {
			int cnt = static_cast<int>(lvldat_buf->get_u32());

			for (int i = 0; i < num_keyframes; ++i) {
				float t = lvldat_buf->get_float();
				if (n == 0) soa->times[i] = t;	// write time once

				int idx = i*16 + n;
				soa->values[idx]      = lvldat_buf->get_float();
				soa->tangent_in[idx]  = lvldat_buf->get_float();
				soa->tangent_out[idx] = lvldat_buf->get_float();
			}
		}

		for (int i = 0; i < num_keyframes; ++i) {
			int idx = i*16 + 15;
			soa->values[idx]      = 0.0f;
			soa->tangent_in[idx]  = 0.0f;
			soa->tangent_out[idx] = 0.0f;
		}

		soa->last_k = 0;
		soa->precompute();

		current_track->segments[seg].left_rail_height  = lvldat_buf->get_float();
		current_track->segments[seg].right_rail_height = lvldat_buf->get_float();
		current_track->segments[seg].left_rail_start = std::clamp(static_cast<float>(lvldat_buf->get_float()), 0.0f, 1.0f);
		current_track->segments[seg].left_rail_end = std::clamp(static_cast<float>(lvldat_buf->get_float()), 0.0f, 1.0f);
		current_track->segments[seg].right_rail_start = std::clamp(static_cast<float>(lvldat_buf->get_float()), 0.0f, 1.0f);
		current_track->segments[seg].right_rail_end = std::clamp(static_cast<float>(lvldat_buf->get_float()), 0.0f, 1.0f);

		// calc segment lengths //

		int sample_per_kf = 32;
		float total_distance = 0.0f;
		RoadTransform latest_sample_pos;
		current_track->segments[seg].curve_matrix->sample(latest_sample_pos, 0.0f);
		for (int i = 0; i < num_keyframes - 1; i++)
		{
			for (int n = 0; n < sample_per_kf; n++)
			{
				float use_t = (float)(n + 1) / sample_per_kf;
				use_t = remap_float(
					use_t,
					0.0f,
					1.0f,
					soa->times[i],
					soa->times[i + 1]
					);
				RoadTransform new_sample_pos;
				current_track->segments[seg].curve_matrix->sample(new_sample_pos, use_t);
				total_distance += latest_sample_pos.t3d.origin.distance_to(new_sample_pos.t3d.origin);
				latest_sample_pos = new_sample_pos;
			}
		}
		current_track->segments[seg].segment_length = total_distance;
		const int bx = 16;
		const int by = 32;
		for (int x = 0; x < bx; x++)
		{
			for (int y = 0; y < by; y += 4)
			{
				SimVec2 use_t[4];
				SimTransform use_pos[4];
				const float tx = (float(x) / (bx - 1)) * 2.0f - 1.0f;
				for (int lane = 0; lane < 4; ++lane) {
					use_t[lane] = SimVec2(tx, float(y + lane) / (by - 1));
				}
				current_track->segments[seg].road_shape->get_oriented_transform_at_time4(use_pos, use_t);
				for (int lane = 0; lane < 4; ++lane) {
					if (use_pos[lane].origin.y < current_track->minimum_y)
					{
						current_track->minimum_y = use_pos[lane].origin.y;
					}
					if (x == 0 && y == 0 && lane == 0)
					{
						current_track->segments[seg].bounds.position = use_pos[lane].origin;
						current_track->segments[seg].bounds.size = SimVec3();
					}
					current_track->segments[seg].bounds.expand_to(use_pos[lane].origin);
					current_track->segments[seg].bounds.expand_to(use_pos[lane].origin + use_pos[lane].basis[1] * 25.f);
				}
			}
		}
		current_track->segments[seg].bounds.grow_by(5.f);
		current_track->segments[seg].checkpoint_start = -1;
		current_track->segments[seg].checkpoint_run_length = 0;
		for (int i = 0; i < current_track->num_checkpoints; i++)
		{
			if (current_track->checkpoints[i].road_segment == seg)
			{
				if (current_track->segments[seg].checkpoint_start == -1)
				{
					current_track->segments[seg].checkpoint_start = i;
				}
				current_track->segments[seg].checkpoint_run_length++;
			}
		}
	}

	current_track->minimum_y -= 250.0f;

	if (trigger_count > 0) {
		current_track->num_trigger_colliders = trigger_count;
		current_track->trigger_colliders = level_data.allocate_array<TriggerCollider*>(trigger_count);
		for (uint32_t t = 0; t < trigger_count; ++t) {
			uint32_t type_val = lvldat_buf->get_u32();
			uint32_t seg_idx  = lvldat_buf->get_u32();
			uint32_t cp_idx   = lvldat_buf->get_u32();

			SimBasis b;
			b[0][0] = lvldat_buf->get_float();
			b[0][1] = lvldat_buf->get_float();
			b[0][2] = lvldat_buf->get_float();
			b[1][0] = lvldat_buf->get_float();
			b[1][1] = lvldat_buf->get_float();
			b[1][2] = lvldat_buf->get_float();
			b[2][0] = lvldat_buf->get_float();
			b[2][1] = lvldat_buf->get_float();
			b[2][2] = lvldat_buf->get_float();
			SimVec3 origin;
			origin.x = lvldat_buf->get_float();
			origin.y = lvldat_buf->get_float();
			origin.z = lvldat_buf->get_float();
			SimTransform inv_t(b, origin);

			SimVec3 ext;
			ext.x = lvldat_buf->get_float();
			ext.y = lvldat_buf->get_float();
			ext.z = lvldat_buf->get_float();

			TriggerCollider* trig = nullptr;
			switch (type_val) {
			case TRIGGER_TYPE::DASHPLATE:
				trig = gamestate_data.allocate_class<Dashplate>();
				break;
			case TRIGGER_TYPE::JUMPPLATE:
				trig = gamestate_data.allocate_class<Jumpplate>();
				break;
			case TRIGGER_TYPE::MINE:
				trig = gamestate_data.allocate_class<Mine>();
				break;
			default:
				// TODO: assert that we never reach here!
				break;
			}
			trig->segment_index = seg_idx;
			trig->checkpoint_index = cp_idx;
			trig->inv_transform = inv_t;
			trig->transform = invert_scaled_transform(inv_t);
			trig->half_extents = ext;
			current_track->trigger_colliders[t] = trig;
		}
	}

	if (mesh_collision_triangle_count > 0) {
		current_track->num_mesh_collision_triangles = static_cast<int>(mesh_collision_triangle_count);
		current_track->mesh_collision_triangles = level_data.allocate_array<TrackMeshCollisionTriangle>(mesh_collision_triangle_count);
		for (uint32_t tri_index = 0; tri_index < mesh_collision_triangle_count; ++tri_index) {
			TrackMeshCollisionTriangle &tri = current_track->mesh_collision_triangles[tri_index];
			tri.terrain = lvldat_buf->get_u32();
			if (terrain_mesh_is_overlay_surface(tri.terrain)) {
				++current_track->num_mesh_overlay_triangles;
			}

			tri.p0.x = lvldat_buf->get_float();
			tri.p0.y = lvldat_buf->get_float();
			tri.p0.z = lvldat_buf->get_float();
			tri.p1.x = lvldat_buf->get_float();
			tri.p1.y = lvldat_buf->get_float();
			tri.p1.z = lvldat_buf->get_float();
			tri.p2.x = lvldat_buf->get_float();
			tri.p2.y = lvldat_buf->get_float();
			tri.p2.z = lvldat_buf->get_float();
			tri.n0.x = lvldat_buf->get_float();
			tri.n0.y = lvldat_buf->get_float();
			tri.n0.z = lvldat_buf->get_float();
			tri.n1.x = lvldat_buf->get_float();
			tri.n1.y = lvldat_buf->get_float();
			tri.n1.z = lvldat_buf->get_float();
			tri.n2.x = lvldat_buf->get_float();
			tri.n2.y = lvldat_buf->get_float();
			tri.n2.z = lvldat_buf->get_float();
			precompute_mesh_triangle_projection(tri, tri_index);

			tri.bounds.position = tri.p0;
			tri.bounds.size = SimVec3();
			tri.bounds.expand_to(tri.p1);
			tri.bounds.expand_to(tri.p2);
			tri.bounds.grow_by(0.1f);
			if (tri.p0.y < current_track->minimum_y) current_track->minimum_y = tri.p0.y;
			if (tri.p1.y < current_track->minimum_y) current_track->minimum_y = tri.p1.y;
			if (tri.p2.y < current_track->minimum_y) current_track->minimum_y = tri.p2.y;
		}
		build_track_mesh_world_bvh(current_track, level_data);
		build_track_mesh_floor_bvh(current_track, level_data);
		build_track_mesh_overlay_bvh(current_track, level_data);
	}


	bumper_track_seed = bumper_track_seed_from_track(current_track);
	bumper_scheduler_lap = 0;
	bumper_next_sequence = 0;
	int requested_cars = requested_cars_hint;
	bumper_count = bumpers_enabled ? BUMPER_POOL_SIZE : 0;
	for (int i = 0; i < BUMPER_POOL_SIZE; ++i) {
		bumper_states[i].active = 0;
		bumper_states[i].spawn_lap = 0;
		bumper_states[i].next_sequence = static_cast<uint32_t>(i);
		bumper_states[i].target_lane = 0.0f;
	}
	PhysicsCarProperties* props_array = nullptr;
	cars = gamestate_data.create_and_allocate_cars(requested_cars, &props_array);
	car_properties_array = props_array;
	num_cars = requested_cars;
	bumper_cars = nullptr;
	bumper_properties_array = nullptr;
	if (bumper_count > 0) {
		bumper_cars = gamestate_data.create_and_allocate_cars(bumper_count, &bumper_properties_array);
	}
	// Build spawn order using an explicit race grid override when one is supplied.
	// Otherwise, use the shared seed to randomize the first Grand Prix race / normal multiplayer race.
	std::vector<int> spawn_order;
	const int grid_car_count = std::max(0, num_cars);
	spawn_order.resize(grid_car_count);
	for (int i = 0; i < grid_car_count; ++i) spawn_order[i] = i;
	bool using_explicit_grid = false;
	if (static_cast<int>(start_grid_slots.size()) >= grid_car_count && grid_car_count > 0) {
		std::vector<uint8_t> used_slots(static_cast<size_t>(grid_car_count), 0);
		using_explicit_grid = true;
		for (int i = 0; i < grid_car_count; ++i) {
			const int slot = start_grid_slots[static_cast<size_t>(i)];
			if (slot < 0 || slot >= grid_car_count || used_slots[static_cast<size_t>(slot)] != 0) {
				using_explicit_grid = false;
				break;
			}
			used_slots[static_cast<size_t>(slot)] = 1;
			spawn_order[i] = slot;
		}
	}
		if (!using_explicit_grid && spawn_seed != 0 && grid_car_count > 1) {
			uint32_t seed = static_cast<uint32_t>(spawn_seed);
			auto next_rand = [&seed]() {
				seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5; return seed;
			};
			for (int i = grid_car_count - 1; i > 0; --i) {
				uint32_t r = next_rand();
				int j = static_cast<int>(r % (i + 1));
				std::swap(spawn_order[i], spawn_order[j]);
			}
		}

		TrackQueryScratch spawn_scratch;
		for (int i = 0; i < num_cars; i++)
		{
			cars[i].soa->current_track[cars[i].soa_index] = current_track;
			if (i < car_prop_buffers.size()) {
				godot::PackedByteArray arr = car_prop_buffers[i];
               // StreamPeerBuffer inherits Reference; using Ref ensures
               // the object is freed when 'pb' goes out of scope.
				godot::Ref<godot::StreamPeerBuffer> pb = godot::Ref<godot::StreamPeerBuffer>(memnew(godot::StreamPeerBuffer));
				pb->set_data_array(arr);
				*(cars[i].soa->car_properties[cars[i].soa_index]) = PhysicsCarProperties::deserialize(*pb);
			}
			if (i < accel_settings.size() && accel_settings[i].get_type() == godot::Variant::FLOAT) {
				cars[i].soa->m_accel_setting[cars[i].soa_index] = accel_settings[i];
			}
			cars[i].initialize_machine();
			cars[i].soa->level_start_time[cars[i].soa_index] += start_countdown_extra_frames;

                // Determine spawn transform at the end of the last track segment
			int seg_idx = current_track->num_segments - 1;
			const int columns = 6;
			const float column_width_start = -0.6f;
			const float column_width_end = 0.6f;
			const float row_spacing = 20.0f;
			const float start_offset = 40.0f;

			int slot = i;
			if (i < grid_car_count) {
				slot = spawn_order[i];
			}
			float distance_back = start_offset + slot * 10;
			while (seg_idx > 0 && distance_back > current_track->segments[seg_idx].segment_length) {
				distance_back -= current_track->segments[seg_idx].segment_length;
				seg_idx -= 1;
			}
			if (seg_idx < 0) {
				seg_idx = 0;
				distance_back = 0.0f;
			}

			const TrackSegment &spawn_seg = current_track->segments[seg_idx];
			float t_y = remap_float(distance_back, 0.0f, spawn_seg.segment_length, 1.0f, 0.0f);
			float t_x = remap_float(static_cast<float>(slot % columns), 0.0f, static_cast<float>(columns - 1), column_width_start, column_width_end);

			SimTransform spawn_transform;
			spawn_seg.road_shape->get_oriented_transform_at_time(spawn_transform, SimVec2(t_x, t_y));
			spawn_transform.basis.orthonormalize();
			spawn_transform.basis = spawn_transform.basis.rotated(spawn_transform.basis.get_column(1), Math_PI);
			const SimVec3 spawn_up = spawn_transform.basis.get_column(1);
			const SimVec3 track_surface_pos = spawn_transform.origin;
			SimVec3 up_offset = spawn_up * 0.5f;
			spawn_transform.origin += up_offset;

			STORE_INDEXED_VEC3(*cars[i].soa, position_current, cars[i].soa_index, spawn_transform.origin);
			STORE_INDEXED_VEC3(*cars[i].soa, position_old, cars[i].soa_index, spawn_transform.origin);
			STORE_INDEXED_VEC3(*cars[i].soa, position_old_2, cars[i].soa_index, spawn_transform.origin);
			STORE_INDEXED_VEC3(*cars[i].soa, position_old_dupe, cars[i].soa_index, spawn_transform.origin);
			STORE_INDEXED_VEC3(*cars[i].soa, initial_pos, cars[i].soa_index, spawn_transform.origin);
			STORE_INDEXED_VEC3(*cars[i].soa, position_bottom, cars[i].soa_index, spawn_transform.xform(SimVec3(0.0f, -0.1f, 0.0f)));

			{ SimTransform mxt_tmp = MXT_LOAD_TRANSFORM(*cars[i].soa, basis_physical, cars[i].soa_index); mxt_tmp.basis = spawn_transform.basis; MXT_STORE_TRANSFORM(*cars[i].soa, basis_physical, cars[i].soa_index, mxt_tmp); }
			{ SimTransform mxt_tmp = MXT_LOAD_TRANSFORM(*cars[i].soa, basis_physical_other, cars[i].soa_index); mxt_tmp.basis = spawn_transform.basis; MXT_STORE_TRANSFORM(*cars[i].soa, basis_physical_other, cars[i].soa_index, mxt_tmp); }
			cars[i].update_pitch_transform_from_machine_front_back();

			MXT_STORE_TRANSFORM(*cars[i].soa, transform_visual, cars[i].soa_index, spawn_transform);
			STORE_INDEXED_VEC3(*cars[i].soa, track_surface_normal, cars[i].soa_index, spawn_up);
			STORE_INDEXED_VEC3(*cars[i].soa, track_surface_pos, cars[i].soa_index, track_surface_pos);
			cars[i].soa->height_above_track[cars[i].soa_index] = 19.5f;

			PhysicsCarSoA *car_soa = cars[i].soa;
			const int car_idx = cars[i].soa_index;
			int spawn_checkpoint = current_track->get_best_checkpoint(spawn_transform.origin, spawn_scratch);
			if (spawn_checkpoint < 0) {
				spawn_checkpoint = current_track->get_best_checkpoint(track_surface_pos, spawn_scratch);
			}
			if (spawn_checkpoint >= 0 && spawn_checkpoint < current_track->num_checkpoints) {
				const CollisionCheckpoint &cur_cp = current_track->checkpoints[spawn_checkpoint];
				const SimVec3 p1 = cur_cp.start_plane.project(spawn_transform.origin);
				const SimVec3 p2 = cur_cp.end_plane.project(spawn_transform.origin);
				const float checkpoint_fraction = get_closest_t_on_segment(spawn_transform.origin, p1, p2);
				const float cp_length = cur_cp.local_distance;
				const float cp_start_distance = cur_cp.distance - cur_cp.local_distance;
				float ground_distance = cp_start_distance + cp_length * std::clamp(checkpoint_fraction, 0.0f, 1.0f);
				float lap_length = current_track->lap_length;
				if (lap_length <= 0.0f && current_track->num_checkpoints > 0) {
					lap_length = current_track->checkpoints[current_track->num_checkpoints - 1].distance;
				}
				if (lap_length > 0.0f) {
					ground_distance = std::fmod(ground_distance, lap_length);
					if (ground_distance < 0.0f) {
						ground_distance += lap_length;
					}
				}
				car_soa->current_checkpoint[car_idx] = static_cast<uint16_t>(spawn_checkpoint);
				car_soa->current_collision_checkpoint[car_idx] = static_cast<int16_t>(spawn_checkpoint);
				car_soa->last_ground_checkpoint[car_idx] = static_cast<uint16_t>(spawn_checkpoint);
				car_soa->checkpoint_fraction[car_idx] = checkpoint_fraction;
				car_soa->lap_progress[car_idx] = (static_cast<float>(spawn_checkpoint) + checkpoint_fraction) / static_cast<float>(current_track->num_checkpoints);
				car_soa->checkpoint_track_distance[car_idx] = ground_distance;
				car_soa->last_ground_distance[car_idx] = ground_distance;
				car_soa->previous_lap_distance[car_idx] = current_track->compute_lap_distance(
					car_soa->current_checkpoint[car_idx],
					car_soa->checkpoint_fraction[car_idx],
					car_soa->lap[car_idx]);
			}
			const int point_base = car_idx * 4;
			const SimVec3 reset_position = spawn_transform.origin;
			const SimBasis& reset_basis = spawn_transform.basis;
			const SimVec3x4 tilt_pos = transform_points_components4(
				reset_basis.c0.x, reset_basis.c0.y, reset_basis.c0.z,
				reset_basis.c1.x, reset_basis.c1.y, reset_basis.c1.z,
				reset_basis.c2.x, reset_basis.c2.y, reset_basis.c2.z,
				reset_position.x, reset_position.y, reset_position.z,
				sim_load4(car_soa->tilt_offset_x + point_base),
				sim_load4(car_soa->tilt_offset_y + point_base),
				sim_load4(car_soa->tilt_offset_z + point_base));
			const SimVec3x4 wall_pos = transform_points_components4(
				reset_basis.c0.x, reset_basis.c0.y, reset_basis.c0.z,
				reset_basis.c1.x, reset_basis.c1.y, reset_basis.c1.z,
				reset_basis.c2.x, reset_basis.c2.y, reset_basis.c2.z,
				reset_position.x, reset_position.y, reset_position.z,
				sim_load4(car_soa->wall_offset_x + point_base),
				sim_load4(car_soa->wall_offset_y + point_base),
				sim_load4(car_soa->wall_offset_z + point_base));
			const SimVec3 wall_sweep_origin = spawn_transform.xform(SimVec3(0.0f, 0.1f, 0.0f));
			for (int point = 0; point < 4; ++point) {
				const int p = point_base + point;
				car_soa->tilt_state[p] = 0;
				car_soa->tilt_force[p] = 0.0f;
				car_soa->tilt_force_spatial_len[p] = 0.0f;
				STORE_INDEXED_VEC3(*car_soa, tilt_force_spatial, p, SimVec3());
				STORE_INDEXED_VEC3(*car_soa, tilt_up_vector_2, p, spawn_up);
				STORE_INDEXED_VEC3(*car_soa, tilt_up_vector, p, spawn_up);
				STORE_INDEXED_VEC3(*car_soa, wall_pos_a, p, wall_sweep_origin);
				STORE_INDEXED_VEC3(*car_soa, wall_collision, p, SimVec3());
			}
			sim_store4(car_soa->tilt_pos_old_x + point_base, tilt_pos.x);
			sim_store4(car_soa->tilt_pos_old_y + point_base, tilt_pos.y);
			sim_store4(car_soa->tilt_pos_old_z + point_base, tilt_pos.z);
			sim_store4(car_soa->tilt_pos_x + point_base, tilt_pos.x);
			sim_store4(car_soa->tilt_pos_y + point_base, tilt_pos.y);
			sim_store4(car_soa->tilt_pos_z + point_base, tilt_pos.z);
			sim_store4(car_soa->wall_pos_b_x + point_base, wall_pos.x);
			sim_store4(car_soa->wall_pos_b_y + point_base, wall_pos.y);
			sim_store4(car_soa->wall_pos_b_z + point_base, wall_pos.z);
			car_soa->machine_state[car_idx] &= ~(MACHINESTATE::AIRBORNE | MACHINESTATE::AIRBORNEMORE0_2S_Q | MACHINESTATE::JUSTLANDED);
			car_soa->air_time[car_idx] = 0;
		}

		for (int i = 0; i < bumper_count; ++i) {
			bumper_cars[i].soa->current_track[bumper_cars[i].soa_index] = current_track;
			configure_bumper_car(i);
			bumper_cars[i].initialize_machine();
			deactivate_bumper_car(i);
		}

		input_buffer = static_cast<PlayerInput*>(malloc(sizeof(PlayerInput) * INPUT_BUFFER_LEN * num_cars));
		for (int i = 0; i < INPUT_BUFFER_LEN * num_cars; i++) {
			input_buffer[i] = PlayerInput::from_neutral();
		}
		ensure_vehicle_tick_soa_capacity(std::max(num_cars, bumper_count));
		if (car_player_ids) {
			::free(car_player_ids);
		}
		if (car_is_cpu) {
			::free(car_is_cpu);
		}
		car_player_ids = static_cast<int32_t*>(malloc(sizeof(int32_t) * num_cars));
		car_is_cpu = static_cast<uint8_t*>(malloc(sizeof(uint8_t) * num_cars));
		for (int i = 0; i < num_cars; ++i) {
			car_player_ids[i] = -1;
			car_is_cpu[i] = 0;
		}

		sim_started = true;

		if (!car_node_container) {
			return;
		}
		if (car_node_container == nullptr) {
			return;
		}
	};

	void GameSim::destroy_gamesim()
	{
		if (sim_started)
		{
			if (current_track) {
				current_track->num_trigger_colliders = 0;
				current_track->trigger_colliders = nullptr;
			}
			if (cars) {
				::free(cars);
				cars = nullptr;
			}
			if (bumper_cars) {
				::free(bumper_cars);
				bumper_cars = nullptr;
			}
			level_data.free_heap();
			gamestate_data.free_heap();
			car_properties_array = nullptr;
			bumper_properties_array = nullptr;
			super_spark_state = nullptr;
			super_sparks = nullptr;
			spark_multimesh_instance = nullptr;
			for (int i = 0; i < STATE_BUFFER_LEN; i++)
			{
				if (state_buffer[i].data)
				{
					::free(state_buffer[i].data);
					state_buffer[i].data = nullptr;
				}
				state_buffer[i].size = 0;
				state_buffer[i].tick = -1;
				state_buffer[i].voice_transform_count = 0;
				state_buffer[i].voice_transforms.clear();
			}
			if (input_buffer) {
				::free(input_buffer);
				input_buffer = nullptr;
			}
			free_vehicle_tick_soa();
			sim_started = false;
			tick = 0;
			current_track = nullptr;
		}
		if (cars) {
			::free(cars);
			cars = nullptr;
		}
		if (bumper_cars) {
			::free(bumper_cars);
			bumper_cars = nullptr;
		}
	if (car_player_ids) {
		::free(car_player_ids);
		car_player_ids = nullptr;
	}
		if (car_is_cpu) {
			::free(car_is_cpu);
			car_is_cpu = nullptr;
		}
		car_render_manager = nullptr;
		gameplay_camera_node = nullptr;
		gameplay_camera.unref();
		gameplay_camera_player_id = -1;
		render_car_transform_nodes.clear();
		render_car_multimeshes.clear();
		render_outline_multimeshes.clear();
		render_outline_main_multimeshes.clear();
		render_shadow_multimeshes.clear();
		render_thruster_multimeshes.clear();
		render_thruster_current_thrust.clear();
		render_car_local_transforms.clear();
		render_outline_local_transforms.clear();
		render_outline_main_local_transforms.clear();
		render_shadow_local_transforms.clear();
		render_thruster_local_transforms.clear();
		render_car_archetype_indices.clear();
		render_car_slots.clear();
		render_visible_car_slots.clear();
		render_visible_thruster_slots.clear();
		render_visible_counts.clear();
		render_visible_thruster_counts.clear();
		render_last_body_instances = 0;
		render_last_thruster_instances = 0;
		render_visual_prev_transforms.clear();
		render_visual_current_transforms.clear();
		render_final_prev_transforms.clear();
		render_final_current_transforms.clear();
		render_visual_prev_ground_distances.clear();
		render_visual_current_ground_distances.clear();
		render_visual_initialized.clear();
		render_rollback_corrections.clear();
		render_rollback_correction_active.clear();
		render_rollback_capture_transforms.clear();
		render_rollback_capture_pending = false;
		render_vehicle_visual_state.clear();
		render_vehicle_effect_refs.clear();
		render_effect_full_flags.clear();
		render_effect_pool_slots.clear();
		for (int i = 0; i < BUMPER_POOL_SIZE; ++i) {
			bumper_states[i].active = 0;
			bumper_states[i].spawn_lap = 0;
			bumper_states[i].next_sequence = static_cast<uint32_t>(i);
			bumper_states[i].target_lane = 0.0f;
		}
		bumper_count = 0;
		bumper_track_seed = 0;
		bumper_scheduler_lap = 0;
		bumper_next_sequence = 0;
		clear_render_thruster_lights();
		native_cpu_drivers.clear();
		race_events.clear();
		cpu_driver_manager = nullptr;
	};

void GameSim::set_car_render_manager(godot::Object* p_car_render_manager)
{
	car_render_manager = p_car_render_manager;
	render_car_multimeshes.clear();
	render_outline_multimeshes.clear();
	render_outline_main_multimeshes.clear();
	render_shadow_multimeshes.clear();
	render_stamp_multimeshes.clear();
	render_thruster_multimeshes.clear();
	render_thruster_current_thrust.clear();
	render_car_local_transforms.clear();
	render_outline_local_transforms.clear();
	render_outline_main_local_transforms.clear();
	render_shadow_local_transforms.clear();
	render_stamp_local_transforms.clear();
	render_thruster_local_transforms.clear();
	render_car_archetype_indices.clear();
	render_car_slots.clear();
	render_visible_car_slots.clear();
	render_visible_thruster_slots.clear();
	render_visible_counts.clear();
	render_visible_thruster_counts.clear();
	render_last_body_instances = 0;
	render_last_thruster_instances = 0;
	render_visual_prev_transforms.clear();
	render_visual_current_transforms.clear();
	render_final_prev_transforms.clear();
	render_final_current_transforms.clear();
	render_visual_prev_ground_distances.clear();
	render_visual_current_ground_distances.clear();
	render_visual_initialized.clear();
	render_rollback_corrections.clear();
	render_rollback_correction_active.clear();
	render_rollback_capture_transforms.clear();
	render_rollback_capture_pending = false;
	render_vehicle_visual_state.clear();
	render_vehicle_effect_refs.clear();
	render_effect_full_flags.clear();
	render_effect_pool_slots.clear();
	clear_render_thruster_lights();
	if (!car_render_manager) {
		return;
	}

	godot::Variant bindings_var = car_render_manager->call("get_native_render_bindings");
	if (bindings_var.get_type() != godot::Variant::DICTIONARY) {
		return;
	}
	godot::Dictionary bindings = bindings_var;
	godot::Array multimeshes = bindings.get("multimeshes", godot::Array());
	godot::Array outline_multimeshes = bindings.get("outline_multimeshes", godot::Array());
	godot::Array outline_main_multimeshes = bindings.get("outline_main_multimeshes", godot::Array());
	godot::Array shadow_multimeshes = bindings.get("shadow_multimeshes", godot::Array());
	godot::Array stamp_multimeshes = bindings.get("stamp_multimeshes", godot::Array());
	godot::Array thruster_multimeshes = bindings.get("thruster_multimeshes", godot::Array());
	godot::Array local_transforms = bindings.get("local_transforms", godot::Array());
	godot::Array outline_local_transforms = bindings.get("outline_local_transforms", godot::Array());
	godot::Array outline_main_local_transforms = bindings.get("outline_main_local_transforms", godot::Array());
	godot::Array shadow_local_transforms = bindings.get("shadow_local_transforms", godot::Array());
	godot::Array stamp_local_transforms = bindings.get("stamp_local_transforms", godot::Array());
	godot::Array thruster_local_transforms = bindings.get("thruster_local_transforms", godot::Array());
	godot::PackedInt32Array archetype_indices = bindings.get("archetype_indices", godot::PackedInt32Array());
	godot::PackedInt32Array slots = bindings.get("slots", godot::PackedInt32Array());

	render_car_multimeshes.reserve(multimeshes.size());
	render_outline_multimeshes.reserve(multimeshes.size());
	render_outline_main_multimeshes.reserve(multimeshes.size());
	render_shadow_multimeshes.reserve(shadow_multimeshes.size());
	render_stamp_multimeshes.reserve(stamp_multimeshes.size());
	render_thruster_multimeshes.reserve(thruster_multimeshes.size());
	render_car_local_transforms.reserve(local_transforms.size());
	render_outline_local_transforms.reserve(local_transforms.size());
	render_outline_main_local_transforms.reserve(local_transforms.size());
	render_shadow_local_transforms.reserve(shadow_local_transforms.size());
	render_stamp_local_transforms.reserve(stamp_local_transforms.size());
	render_thruster_local_transforms.reserve(thruster_local_transforms.size());
	for (int i = 0; i < multimeshes.size(); ++i) {
		godot::Ref<godot::MultiMesh> multimesh = multimeshes[i];
		render_car_multimeshes.push_back(multimesh);
		godot::Ref<godot::MultiMesh> outline_multimesh;
		if (i < outline_multimeshes.size()) {
			outline_multimesh = outline_multimeshes[i];
		}
		render_outline_multimeshes.push_back(outline_multimesh);
		godot::Ref<godot::MultiMesh> outline_main_multimesh;
		if (i < outline_main_multimeshes.size()) {
			outline_main_multimesh = outline_main_multimeshes[i];
		}
		render_outline_main_multimeshes.push_back(outline_main_multimesh);
		godot::Ref<godot::MultiMesh> shadow_multimesh;
		if (i < shadow_multimeshes.size()) {
			shadow_multimesh = shadow_multimeshes[i];
		}
		render_shadow_multimeshes.push_back(shadow_multimesh);
		godot::Ref<godot::MultiMesh> stamp_multimesh;
		if (i < stamp_multimeshes.size()) {
			stamp_multimesh = stamp_multimeshes[i];
		}
		render_stamp_multimeshes.push_back(stamp_multimesh);
		godot::Ref<godot::MultiMesh> thruster_multimesh;
		if (i < thruster_multimeshes.size()) {
			thruster_multimesh = thruster_multimeshes[i];
		}
		render_thruster_multimeshes.push_back(thruster_multimesh);
		if (i < local_transforms.size() && local_transforms[i].get_type() == godot::Variant::TRANSFORM3D) {
			render_car_local_transforms.push_back(sim_transform(local_transforms[i]));
		} else {
			render_car_local_transforms.push_back(SimTransform());
		}
		if (i < outline_local_transforms.size() && outline_local_transforms[i].get_type() == godot::Variant::TRANSFORM3D) {
			render_outline_local_transforms.push_back(sim_transform(outline_local_transforms[i]));
		} else {
			render_outline_local_transforms.push_back(SimTransform());
		}
		if (i < outline_main_local_transforms.size() && outline_main_local_transforms[i].get_type() == godot::Variant::TRANSFORM3D) {
			render_outline_main_local_transforms.push_back(sim_transform(outline_main_local_transforms[i]));
		} else {
			render_outline_main_local_transforms.push_back(SimTransform());
		}
		if (i < shadow_local_transforms.size() && shadow_local_transforms[i].get_type() == godot::Variant::TRANSFORM3D) {
			render_shadow_local_transforms.push_back(sim_transform(shadow_local_transforms[i]));
		} else {
			render_shadow_local_transforms.push_back(SimTransform());
		}
		if (i < stamp_local_transforms.size() && stamp_local_transforms[i].get_type() == godot::Variant::TRANSFORM3D) {
			render_stamp_local_transforms.push_back(sim_transform(stamp_local_transforms[i]));
		} else {
			render_stamp_local_transforms.push_back(SimTransform());
		}
		std::vector<SimTransform> local_thrusters;
		if (i < thruster_local_transforms.size() && thruster_local_transforms[i].get_type() == godot::Variant::ARRAY) {
			godot::Array transforms = thruster_local_transforms[i];
			local_thrusters.reserve(transforms.size());
			for (int t = 0; t < transforms.size(); ++t) {
				if (transforms[t].get_type() == godot::Variant::TRANSFORM3D) {
					local_thrusters.push_back(sim_transform(transforms[t]));
				}
			}
		}
		render_thruster_local_transforms.push_back(std::move(local_thrusters));
	}

	render_car_archetype_indices.resize(archetype_indices.size());
	for (int i = 0; i < archetype_indices.size(); ++i) {
		render_car_archetype_indices[i] = archetype_indices[i];
	}
	render_car_slots.resize(slots.size());
	for (int i = 0; i < slots.size(); ++i) {
		render_car_slots[i] = slots[i];
	}
	render_visible_car_slots.assign(render_car_slots.size(), -1);
	render_visible_thruster_slots.assign(render_car_slots.size(), -1);
	render_visible_counts.assign(render_car_multimeshes.size(), 0);
	render_visible_thruster_counts.assign(render_thruster_multimeshes.size(), 0);
	cache_native_visual_effect_nodes();
}

void GameSim::clear_render_thruster_lights()
{
	RenderingServer* rs = RenderingServer::get_singleton();
	if (rs) {
		for (RenderThrusterLightRID& light : render_thruster_lights) {
			if (light.instance.is_valid()) {
				rs->free_rid(light.instance);
			}
			if (light.light.is_valid()) {
				rs->free_rid(light.light);
			}
		}
	}
	render_thruster_lights.clear();
	render_thruster_light_scenario = RID();
	render_thruster_light_visible_count = 0;
}

void GameSim::ensure_render_thruster_light_capacity(int capacity)
{
	if (capacity <= static_cast<int>(render_thruster_lights.size())) {
		return;
	}
	RenderingServer* rs = RenderingServer::get_singleton();
	if (!rs || !car_node_container) {
		return;
	}
	Ref<World3D> world = car_node_container->get_world_3d();
	if (world.is_null()) {
		return;
	}
	const RID scenario = world->get_scenario();
	if (!scenario.is_valid()) {
		return;
	}
	if (render_thruster_light_scenario.is_valid() && render_thruster_light_scenario != scenario) {
		clear_render_thruster_lights();
	}
	render_thruster_light_scenario = scenario;
	render_thruster_lights.reserve(capacity);
	while (static_cast<int>(render_thruster_lights.size()) < capacity) {
		RenderThrusterLightRID item;
		item.light = rs->omni_light_create();
		rs->light_set_color(item.light, Color(0.3f, 0.7f, 1.0f, 1.0f));
		rs->light_set_param(item.light, RenderingServer::LIGHT_PARAM_RANGE, 64.0);
		rs->light_set_param(item.light, RenderingServer::LIGHT_PARAM_ENERGY, 0.0);
		rs->light_set_param(item.light, RenderingServer::LIGHT_PARAM_ATTENUATION, 1.0);
		rs->light_set_shadow(item.light, false);
		rs->light_set_cull_mask(item.light, 2);
		item.instance = rs->instance_create2(item.light, render_thruster_light_scenario);
		rs->instance_set_layer_mask(item.instance, 2);
		rs->instance_set_visible(item.instance, false);
		render_thruster_lights.push_back(item);
	}
}

void GameSim::hide_unused_render_thruster_lights(int used_count)
{
	RenderingServer* rs = RenderingServer::get_singleton();
	if (!rs) {
		return;
	}
	if (used_count < 0) {
		used_count = 0;
	}
	if (used_count > static_cast<int>(render_thruster_lights.size())) {
		used_count = static_cast<int>(render_thruster_lights.size());
	}
	for (int i = used_count; i < render_thruster_light_visible_count && i < static_cast<int>(render_thruster_lights.size()); ++i) {
		rs->instance_set_visible(render_thruster_lights[i].instance, false);
	}
	render_thruster_light_visible_count = used_count;
}

void GameSim::cache_native_visual_effect_nodes()
{
	render_vehicle_effect_refs.clear();
	render_effect_full_flags.clear();
	render_effect_pool_slots.clear();
	const int car_count = std::max(0, num_cars);
	render_vehicle_effect_refs.resize(car_count);
	render_effect_full_flags.resize(car_count);
	if (!car_node_container) {
		return;
	}
	TypedArray<godot::Node> visual_nodes = car_node_container->get_children();
	for (int i = 0; i < visual_nodes.size(); ++i) {
		Node* car_node = Object::cast_to<Node>(visual_nodes[i]);
		if (!car_node) {
			continue;
		}
		car_node->set_process(false);
		car_node->set_physics_process(false);
		const Variant slot_var = car_node->get(StringName("effect_pool_slot"));
		if (slot_var.get_type() != Variant::INT) {
			continue;
		}
		const int slot_index = static_cast<int>(static_cast<int64_t>(slot_var));
		const bool local_visual = static_cast<bool>(car_node->get(StringName("local_visual_enabled")));
		int output_slot = slot_index;
		if (slot_index < 0 && local_visual) {
			output_slot = static_cast<int>(render_effect_pool_slots.size());
		}
		if (output_slot < 0) {
			continue;
		}
		if (static_cast<int>(render_effect_pool_slots.size()) <= output_slot) {
			render_effect_pool_slots.resize(output_slot + 1);
		}
		RenderEffectPoolSlot& slot = render_effect_pool_slots[output_slot];
		slot.car_index = -1;
		slot.fixed_local = local_visual ? 1 : 0;
		slot.node = car_node;
		slot.car_transform = Object::cast_to<Node3D>(car_node->get_node_or_null(NodePath("CarTransform")));
		if (slot.car_transform) {
			slot.recharge_particles = Object::cast_to<GPUParticles3D>(slot.car_transform->get_node_or_null(NodePath("RechargeParticles")));
			slot.attack_particles = Object::cast_to<GPUParticles3D>(slot.car_transform->get_node_or_null(NodePath("AttackParticles")));
			slot.landing_particles = Object::cast_to<GPUParticles3D>(slot.car_transform->get_node_or_null(NodePath("LandingParticles")));
			slot.damage_electricity = Object::cast_to<GPUParticles3D>(slot.car_transform->get_node_or_null(NodePath("DamageElectricity")));
			if (slot.damage_electricity) {
				slot.damage_smoke = Object::cast_to<GPUParticles3D>(slot.damage_electricity->get_node_or_null(NodePath("DamageSmoke")));
				slot.damage_electricity_material = slot.damage_electricity->get_process_material();
			}
		}
		slot.boost_electricity = Object::cast_to<Object>(car_node->get_node_or_null(NodePath("BoostElectricity")));
		if (slot.recharge_particles) {
			slot.recharge_particles->set_emitting(false);
		}
		if (slot.attack_particles) {
			slot.attack_particles->set_emitting(false);
		}
		if (slot.landing_particles) {
			slot.landing_particles->set_emitting(false);
		}
		if (slot.damage_electricity) {
			slot.damage_electricity->set_emitting(false);
			slot.damage_electricity->set_amount_ratio(0.0);
			slot.damage_electricity->set_visible(false);
		}
		if (slot.damage_smoke) {
			slot.damage_smoke->set_emitting(false);
			slot.damage_smoke->set_amount_ratio(0.0);
		}
		if (slot.boost_electricity) {
			slot.boost_electricity->set("boosting", false);
			slot.boost_electricity->set("visible", false);
		}
	}
}

void GameSim::set_gameplay_camera(godot::Camera3D* p_camera, int player_id)
{
	gameplay_camera_node = p_camera;
	gameplay_camera_player_id = player_id;
	if (gameplay_camera.is_null()) {
		gameplay_camera.instantiate();
	}
	if (gameplay_camera.is_valid()) {
		gameplay_camera->reset();
	}
	if (gameplay_camera_node) {
		gameplay_camera_node->make_current();
		gameplay_camera_node->set_near(0.25f);
		gameplay_camera_node->set_far(40000.0f);
	}
}

void GameSim::set_cpu_driver_manager(godot::Object* manager)
{
	cpu_driver_manager = manager;
}

GameSim::NativeCpuDriverState* GameSim::find_native_cpu_driver(int32_t player_id)
{
	for (NativeCpuDriverState& driver : native_cpu_drivers) {
		if (driver.active && driver.player_id == player_id) {
			return &driver;
		}
	}
	return nullptr;
}

void GameSim::configure_native_cpu_drivers()
{
	native_cpu_drivers.clear();
	native_cpu_drivers.resize(std::max(0, num_cars));
	const godot::PackedByteArray neutral = PlayerInput::to_bytes(PlayerInput::from_neutral());
	for (int i = 0; i < num_cars; ++i) {
		NativeCpuDriverState& driver = native_cpu_drivers[i];
		driver.player_id = car_player_ids ? car_player_ids[i] : -1;
		driver.active = (car_is_cpu && car_is_cpu[i] && driver.player_id != -1) ? 1 : 0;
		driver.last_generated_tick = -1;
		driver.pending_input = neutral;
	}
}

static inline uint32_t native_cpu_hash_u32(uint32_t x)
{
	x ^= x >> 16;
	x *= 0x7feb352du;
	x ^= x >> 15;
	x *= 0x846ca68bu;
	x ^= x >> 16;
	return x;
}

static inline float native_cpu_rand01_from_seed(uint32_t seed)
{
	return static_cast<float>(native_cpu_hash_u32(seed) & 0x00FFFFFFu) * (1.0f / 16777215.0f);
}

static inline float native_cpu_smooth_noise_signed(uint32_t seed_base, int expected_tick, int period_ticks)
{
	const int t0 = expected_tick / period_ticks;
	const int t1 = t0 + 1;
	const float frac = static_cast<float>(expected_tick - t0 * period_ticks) / static_cast<float>(period_ticks);
	const float smooth = frac * frac * (3.0f - 2.0f * frac);
	const float a = native_cpu_rand01_from_seed(seed_base ^ (static_cast<uint32_t>(t0) * 0x27D4EB2Du)) * 2.0f - 1.0f;
	const float b = native_cpu_rand01_from_seed(seed_base ^ (static_cast<uint32_t>(t1) * 0x27D4EB2Du)) * 2.0f - 1.0f;
	return a + (b - a) * smooth;
}

static inline PlayerInput native_cpu_generate_input_for_car(const PhysicsCar& car, int32_t player_id, int expected_tick, int spawn_seed)
{
	PhysicsCarSoA& soa = *car.soa;
	const int i = car.soa_index;
	const SimBasis physical_basis = MXT_LOAD_TRANSFORM(soa, basis_physical, i).basis;
	SimBasis surface = soa.road_sample[i].closest_surface.basis;
	float road_tx = soa.road_sample[i].road_t.x;
	if (soa.current_track[i]) {
		int sample_cp = soa.current_collision_checkpoint[i];
		if (sample_cp < 0 || sample_cp >= soa.current_track[i]->num_checkpoints) {
			sample_cp = soa.current_checkpoint[i];
		}
		if (sample_cp >= 0 && sample_cp < soa.current_track[i]->num_checkpoints) {
			const CollisionCheckpoint &cp = soa.current_track[i]->checkpoints[sample_cp];
			const SimVec3 pos = LOAD_INDEXED_VEC3(soa, position_current, i);
			const SimVec3 p1 = cp.start_plane.project(pos);
			const SimVec3 p2 = cp.end_plane.project(pos);
			const SimVec3 span = p2 - p1;
			const float span_len2 = span.length_squared();
			float cp_t = 0.0f;
			if (span_len2 > 1.0e-6f) {
				cp_t = (pos - p1).dot(span) / span_len2;
				cp_t = std::max(0.0f, std::min(1.0f, cp_t));
			}
			surface[0] = cp.orientation_start[0].lerp(cp.orientation_end[0], cp_t);
			surface[1] = cp.orientation_start[1].lerp(cp.orientation_end[1], cp_t);
			surface[2] = cp.orientation_start[2].lerp(cp.orientation_end[2], cp_t);
			const SimVec3 center = cp.position_start.lerp(cp.position_end, cp_t);
			const float x_radius_inv = lerp(cp.x_radius_start_inv, cp.x_radius_end_inv, cp_t);
			road_tx = (pos - center).dot(surface[0]) * x_radius_inv;
		}
	}
	const float energy = soa.energy[i];
	const uint32_t tilt_state = soa.tilt_state[i * 4 + 1];
	const uint32_t seed_base =
		static_cast<uint32_t>(player_id) * 0x9E3779B9u ^
		static_cast<uint32_t>(expected_tick) * 0x85EBCA6Bu ^
		static_cast<uint32_t>(spawn_seed) * 0xC2B2AE35u;
	const uint32_t lane_seed =
		static_cast<uint32_t>(player_id) * 0x9E3779B9u ^
		static_cast<uint32_t>(spawn_seed) * 0xC2B2AE35u ^
		0xA341316Cu;

	PlayerInput input = PlayerInput::from_neutral();
	input.accelerate = 1.0f;

	float desired_steer = (physical_basis.c0 + surface.c0).dot(surface.c2);
	const float desired_lane = native_cpu_smooth_noise_signed(lane_seed, expected_tick, 480) * 0.8f;

	const float lane_offset = road_tx + desired_lane;
	input.strafe_left = std::max(0.0f, std::min(1.0f, std::abs(std::min(lane_offset, 0.0f)) * 4.0f));
	input.strafe_right = std::max(0.0f, std::min(1.0f, std::max(lane_offset, 0.0f) * 4.0f));

	const bool drifting = (tilt_state & 0x4u) != 0;
	const bool wants_drift = std::abs(desired_steer) >= 0.4f && !drifting;
	if (drifting) {
		desired_steer *= 5.0f;
	}
	input.steer_horizontal = std::max(-1.0f, std::min(1.0f, desired_steer * 30.0f));
	if (wants_drift) {
		input.strafe_left = 1.0f;
		input.strafe_right = 1.0f;
	}

	const uint32_t boost_phase = native_cpu_hash_u32(seed_base ^ 0xB5297A4Du) % 720u;
	const bool wants_boost = energy > 10.0f && boost_phase == 0u;
	input.boost = wants_boost;

	return input;
}

void GameSim::update_native_cpu_driver(int car_index)
{
	if (car_index < 0 || car_index >= num_cars || car_index >= static_cast<int>(native_cpu_drivers.size())) {
		return;
	}
	NativeCpuDriverState& driver = native_cpu_drivers[car_index];
	if (!driver.active) {
		return;
	}

	PlayerInput input = native_cpu_generate_input_for_car(cars[car_index], driver.player_id, tick, spawn_seed);
	driver.pending_input = PlayerInput::to_bytes(input);
	driver.last_generated_tick = tick;
}

void GameSim::update_native_cpu_drivers()
{
	if (!cars || native_cpu_drivers.empty()) {
		return;
	}
	for (int i = 0; i < num_cars; ++i) {
		update_native_cpu_driver(i);
	}
}

godot::PackedByteArray GameSim::get_native_cpu_input_for_tick(int player_id, int expected_tick)
{
	return generate_native_cpu_input_for_tick(player_id, expected_tick);
}

godot::PackedByteArray GameSim::generate_native_cpu_input_for_tick(int player_id, int expected_tick)
{
	NativeCpuDriverState* driver = find_native_cpu_driver(static_cast<int32_t>(player_id));
	for (int car_index = 0; car_index < num_cars; ++car_index) {
		if (car_player_ids && car_player_ids[car_index] == player_id) {
			PlayerInput input = native_cpu_generate_input_for_car(cars[car_index], static_cast<int32_t>(player_id), expected_tick, spawn_seed);
			godot::PackedByteArray input_bytes = PlayerInput::to_bytes(input);
			if (driver) {
				driver->pending_input = input_bytes;
				driver->last_generated_tick = expected_tick;
			}
			return input_bytes;
		}
	}
	return PlayerInput::to_bytes(PlayerInput::from_neutral());
}

godot::Dictionary GameSim::get_input_frame_as_dictionary(int target_tick) const
{
	godot::Dictionary out;
	if (!input_buffer || !car_player_ids || num_cars <= 0 || target_tick < 0) {
		return out;
	}
	if (target_tick >= tick || tick - target_tick > INPUT_BUFFER_LEN) {
		return out;
	}
	const int buf_index = target_tick % INPUT_BUFFER_LEN;
	const PlayerInput* frame = input_buffer + buf_index * num_cars;
	for (int i = 0; i < num_cars; ++i) {
		out[car_player_ids[i]] = PlayerInput::to_bytes(frame[i]);
	}
	return out;
}

void GameSim::update_render_visual_snapshots(int visual_count)
{
	if (visual_count <= 0 || !cars) {
		return;
	}
	if (static_cast<int>(render_visual_prev_transforms.size()) != visual_count) {
		render_visual_prev_transforms.resize(visual_count);
		render_visual_current_transforms.resize(visual_count);
		render_final_prev_transforms.resize(visual_count);
		render_final_current_transforms.resize(visual_count);
		render_visual_prev_ground_distances.resize(visual_count);
		render_visual_current_ground_distances.resize(visual_count);
		render_visual_initialized.assign(visual_count, 0);
		render_rollback_corrections.assign(visual_count, SimTransform());
		render_rollback_correction_active.assign(visual_count, 0);
		render_vehicle_visual_state.assign(visual_count, RenderVehicleVisualState());
		render_rollback_capture_transforms.clear();
		render_rollback_capture_pending = false;
	}
	for (int i = 0; i < visual_count; ++i) {
		PhysicsCar* visual_car = nullptr;
		if (i < num_cars) {
			visual_car = &cars[i];
		} else if (bumper_cars && i < num_cars + bumper_count) {
			const int bumper_slot = i - num_cars;
			if (bumper_states[bumper_slot].active) {
				visual_car = &bumper_cars[bumper_slot];
			}
		}
		if (!visual_car) {
			render_visual_initialized[i] = 0;
			render_visual_prev_transforms[i] = SimTransform();
			render_visual_current_transforms[i] = SimTransform();
			render_final_prev_transforms[i] = SimTransform();
			render_final_current_transforms[i] = SimTransform();
			render_visual_prev_ground_distances[i] = 20.0f;
			render_visual_current_ground_distances[i] = 20.0f;
			continue;
		}
		update_machine_visual_transform_for_render(*visual_car->soa, visual_car->soa_index, render_vehicle_visual_state[i]);
		PhysicsCarSoA& soa = *visual_car->soa;
		const int lane = visual_car->soa_index;
		const SimTransform current = MXT_LOAD_TRANSFORM(soa, transform_visual, lane);
		float current_ground_distance = 20.0f - soa.height_above_track[lane];
		if (current_ground_distance < 0.0f) {
			current_ground_distance = 0.0f;
		}
		if (current_ground_distance > 20.0f) {
			current_ground_distance = 20.0f;
		}
		const bool was_initialized = render_visual_initialized[i] != 0;
		if (was_initialized) {
			render_visual_prev_transforms[i] = render_visual_current_transforms[i];
			if (i < static_cast<int>(render_final_prev_transforms.size()) &&
					i < static_cast<int>(render_final_current_transforms.size())) {
				render_final_prev_transforms[i] = render_final_current_transforms[i];
			}
			render_visual_prev_ground_distances[i] = render_visual_current_ground_distances[i];
		} else {
			render_visual_prev_transforms[i] = current;
			if (i < static_cast<int>(render_final_prev_transforms.size())) {
				render_final_prev_transforms[i] = current;
			}
			render_visual_prev_ground_distances[i] = current_ground_distance;
			render_visual_initialized[i] = 1;
		}
		render_visual_current_transforms[i] = current;
		render_visual_current_ground_distances[i] = current_ground_distance;
		if (i < static_cast<int>(render_rollback_correction_active.size()) && render_rollback_correction_active[i]) {
			render_rollback_corrections[i] = interpolate_sim_transform(render_rollback_corrections[i], SimTransform(), 0.3f);
			if (render_correction_is_small(render_rollback_corrections[i])) {
				render_rollback_corrections[i] = SimTransform();
				render_rollback_correction_active[i] = 0;
			}
		}
		SimTransform final_transform = current;
		if (i < static_cast<int>(render_rollback_correction_active.size()) &&
				render_rollback_correction_active[i] &&
				i < static_cast<int>(render_rollback_corrections.size())) {
			final_transform = apply_render_correction(current, render_rollback_corrections[i]);
		}
		if (i < static_cast<int>(render_final_current_transforms.size())) {
			render_final_current_transforms[i] = final_transform;
			if (!was_initialized && i < static_cast<int>(render_final_prev_transforms.size())) {
				render_final_prev_transforms[i] = final_transform;
			}
		}
	}
}

void GameSim::apply_render_multimeshes(float alpha)
{
	const int render_binding_count = static_cast<int>(render_car_archetype_indices.size());
	const int visual_count = std::min(render_binding_count, static_cast<int>(render_final_current_transforms.size()));
	if (render_visible_car_slots.size() < static_cast<size_t>(visual_count)) {
		render_visible_car_slots.resize(visual_count, -1);
	}
	if (render_visible_thruster_slots.size() < static_cast<size_t>(visual_count)) {
		render_visible_thruster_slots.resize(visual_count, -1);
	}
	if (render_visible_counts.size() < render_car_multimeshes.size()) {
		render_visible_counts.resize(render_car_multimeshes.size(), 0);
	}
	if (render_visible_thruster_counts.size() < render_thruster_multimeshes.size()) {
		render_visible_thruster_counts.resize(render_thruster_multimeshes.size(), 0);
	}
	render_last_body_instances = 0;
	render_last_thruster_instances = 0;
	std::fill(render_visible_counts.begin(), render_visible_counts.end(), 0);
	std::fill(render_visible_thruster_counts.begin(), render_visible_thruster_counts.end(), 0);
	Camera3D* camera = gameplay_camera_node;
	SimVec3 camera_origin;
	SimVec3 camera_right;
	SimVec3 camera_up;
	SimVec3 camera_forward;
	float camera_tan_half_fov = 1.0f;
	float camera_aspect = 4.0f / 3.0f;
	float camera_far = 40000.0f;
	if (camera) {
		const Transform3D camera_transform = camera->get_global_transform();
		camera_origin = sim_vec3(camera_transform.origin);
		camera_right = sim_vec3(camera_transform.basis.get_column(0)).normalized();
		camera_up = sim_vec3(camera_transform.basis.get_column(1)).normalized();
		camera_forward = sim_vec3(camera_transform.basis.get_column(2)) * -1.0f;
		camera_forward = camera_forward.normalized();
		camera_tan_half_fov = std::tan(static_cast<float>(camera->get_fov()) * (TAU / 360.0f) * 0.5f);
		camera_far = static_cast<float>(camera->get_far());
		if (Viewport* viewport = camera->get_viewport()) {
			const Vector2 size = viewport->get_visible_rect().size;
			if (size.y > 0.0f) {
				camera_aspect = static_cast<float>(size.x / size.y);
			}
		}
	}
	constexpr float CAR_VISIBILITY_RADIUS = 32.0f;
	constexpr float BODY_LOD_MAX_DISTANCE_SQ = 360.0f * 360.0f;
	for (int i = 0; i < visual_count; ++i) {
		PhysicsCar* visual_car = nullptr;
		if (i < num_cars) {
			visual_car = &cars[i];
		} else if (bumper_cars && i < num_cars + bumper_count) {
			const int bumper_slot = i - num_cars;
			if (bumper_states[bumper_slot].active) {
				visual_car = &bumper_cars[bumper_slot];
			}
		}
		if (!visual_car) {
			render_visible_car_slots[i] = -1;
			render_visible_thruster_slots[i] = -1;
			continue;
		}
		if (i >= static_cast<int>(render_car_archetype_indices.size()) || i >= static_cast<int>(render_car_slots.size())) {
			render_visible_car_slots[i] = -1;
			render_visible_thruster_slots[i] = -1;
			continue;
		}
		const int archetype = render_car_archetype_indices[i];
		const int slot = render_car_slots[i];
		if (archetype < 0 || archetype >= static_cast<int>(render_car_multimeshes.size()) ||
				archetype >= static_cast<int>(render_car_local_transforms.size()) ||
				render_car_multimeshes[archetype].is_null() || slot < 0) {
			render_visible_car_slots[i] = -1;
			render_visible_thruster_slots[i] = -1;
			continue;
		}
		SimTransform visual_transform = interpolate_sim_transform(
			render_final_prev_transforms[i],
			render_final_current_transforms[i],
			alpha);
		render_visible_thruster_slots[i] = -1;
		if (archetype < static_cast<int>(render_thruster_multimeshes.size()) &&
				archetype < static_cast<int>(render_thruster_local_transforms.size()) &&
				archetype < static_cast<int>(render_visible_thruster_counts.size()) &&
				render_thruster_multimeshes[archetype].is_valid()) {
			const std::vector<SimTransform>& thruster_locals = render_thruster_local_transforms[archetype];
			const int thruster_count = static_cast<int>(thruster_locals.size());
			const int thruster_visible_slot = render_visible_thruster_counts[archetype]++;
			render_visible_thruster_slots[i] = thruster_visible_slot;
			const float thrust = i < static_cast<int>(render_thruster_current_thrust.size()) ? render_thruster_current_thrust[i] : 0.0f;
			for (int t = 0; t < thruster_count; ++t) {
				const int thruster_slot = thruster_visible_slot * thruster_count + t;
				const SimTransform thruster_transform = visual_transform * thruster_locals[t];
				render_thruster_multimeshes[archetype]->set_instance_transform(thruster_slot, gd_transform(thruster_transform));
				render_thruster_multimeshes[archetype]->set_instance_color(thruster_slot, godot::Color(thrust, thrust, thrust, thrust));
				render_thruster_multimeshes[archetype]->set_instance_custom_data(thruster_slot, godot::Color(thrust * 0.2f, static_cast<float>((tick + t) & 255) * 0.0245436926f, thrust, 1.0f));
			}
		}
		bool visible = true;
		if (camera) {
			const SimVec3 camera_to_car = visual_transform.origin - camera_origin;
			const float camera_distance_sq = camera_to_car.length_squared();
			const float forward_distance = camera_to_car.dot(camera_forward);
			const float right_distance = camera_to_car.dot(camera_right);
			const float up_distance = camera_to_car.dot(camera_up);
			const float depth_for_width = std::max(forward_distance, 0.0f);
			visible =
				forward_distance >= -CAR_VISIBILITY_RADIUS &&
				forward_distance <= camera_far + CAR_VISIBILITY_RADIUS &&
				std::abs(right_distance) <= depth_for_width * camera_tan_half_fov * camera_aspect + CAR_VISIBILITY_RADIUS &&
				std::abs(up_distance) <= depth_for_width * camera_tan_half_fov + CAR_VISIBILITY_RADIUS;
			visible = visible && camera_distance_sq <= BODY_LOD_MAX_DISTANCE_SQ;
		}
		if (!visible) {
			render_visible_car_slots[i] = -1;
			continue;
		}
		const int visible_slot = render_visible_counts[archetype]++;
		render_visible_car_slots[i] = visible_slot;
		PhysicsCarSoA& soa = *visual_car->soa;
		const int lane = visual_car->soa_index;
		godot::Color body_overlay(0, 0, 0, 1);
		if (i < static_cast<int>(render_vehicle_effect_refs.size())) {
			body_overlay = render_vehicle_effect_refs[i].overlay;
			body_overlay.r += render_vehicle_effect_refs[i].energy_overlay.r;
			body_overlay.g += render_vehicle_effect_refs[i].energy_overlay.g;
			body_overlay.b += render_vehicle_effect_refs[i].energy_overlay.b;
			body_overlay.a = 1.0f;
		}
		SimVec3 outline_velocity = LOAD_INDEXED_VEC3(soa, position_old, lane) - LOAD_INDEXED_VEC3(soa, position_current, lane);
		const float outline_speed = outline_velocity.length();
		if (outline_speed <= 0.0001f) {
			outline_velocity = MXT_LOAD_TRANSFORM(soa, basis_physical, lane).basis.get_column(2) * 0.01f;
		} else {
			outline_velocity = outline_velocity * ((std::max(outline_speed - 4.0f, 0.0f) * 0.5f) / outline_speed) +
				MXT_LOAD_TRANSFORM(soa, basis_physical, lane).basis.get_column(2) * 0.01f;
		}
		if (render_car_multimeshes[archetype].is_valid()) {
			const SimTransform instance_transform = visual_transform * render_car_local_transforms[archetype];
			render_car_multimeshes[archetype]->set_instance_transform(visible_slot, gd_transform(instance_transform));
			render_car_multimeshes[archetype]->set_instance_color(visible_slot, body_overlay);
			render_car_multimeshes[archetype]->set_instance_custom_data(visible_slot, godot::Color(0, 0, 0, 1));
			}
		if (archetype < static_cast<int>(render_stamp_multimeshes.size()) &&
				archetype < static_cast<int>(render_stamp_local_transforms.size()) &&
				render_stamp_multimeshes[archetype].is_valid()) {
			const SimTransform stamp_transform = visual_transform * render_stamp_local_transforms[archetype];
			render_stamp_multimeshes[archetype]->set_instance_transform(visible_slot, gd_transform(stamp_transform));
			render_stamp_multimeshes[archetype]->set_instance_color(visible_slot, godot::Color(1, 1, 1, 1));
			render_stamp_multimeshes[archetype]->set_instance_custom_data(visible_slot, godot::Color(0, 0, 0, 1));
		}
			if (archetype < static_cast<int>(render_outline_multimeshes.size()) &&
					archetype < static_cast<int>(render_outline_local_transforms.size()) &&
					render_outline_multimeshes[archetype].is_valid()) {
				const SimTransform outline_transform = visual_transform * render_outline_local_transforms[archetype];
				const float boost_outline = std::max(0.0f, std::min(1.0f, soa.boost_frames[lane] * 0.005f));
				render_outline_multimeshes[archetype]->set_instance_transform(visible_slot, gd_transform(outline_transform));
				render_outline_multimeshes[archetype]->set_instance_custom_data(visible_slot, godot::Color(outline_velocity.x, outline_velocity.y, outline_velocity.z, 1.0f));
				render_outline_multimeshes[archetype]->set_instance_color(visible_slot, godot::Color(0.5f * boost_outline, 0.7f * boost_outline, 1.0f * boost_outline, 1.0f));
			}
		if (archetype < static_cast<int>(render_outline_main_multimeshes.size()) &&
				archetype < static_cast<int>(render_outline_main_local_transforms.size()) &&
				render_outline_main_multimeshes[archetype].is_valid()) {
			const SimTransform outline_transform = visual_transform * render_outline_main_local_transforms[archetype];
			render_outline_main_multimeshes[archetype]->set_instance_transform(visible_slot, gd_transform(outline_transform));
			render_outline_main_multimeshes[archetype]->set_instance_custom_data(visible_slot, godot::Color(outline_velocity.x, outline_velocity.y, outline_velocity.z, 1.0f));
			render_outline_main_multimeshes[archetype]->set_instance_color(visible_slot, godot::Color(0, 0, 0, 1));
		}
		if (archetype < static_cast<int>(render_shadow_multimeshes.size()) &&
				archetype < static_cast<int>(render_shadow_local_transforms.size()) &&
				render_shadow_multimeshes[archetype].is_valid()) {
			const float prev_ground_distance = i < static_cast<int>(render_visual_prev_ground_distances.size()) ? render_visual_prev_ground_distances[i] : 20.0f;
			const float current_ground_distance = i < static_cast<int>(render_visual_current_ground_distances.size()) ? render_visual_current_ground_distances[i] : prev_ground_distance;
			const float ground_distance = prev_ground_distance + (current_ground_distance - prev_ground_distance) * alpha;
			SimVec3 shadow_normal = LOAD_INDEXED_VEC3(soa, track_surface_normal, lane);
			if (shadow_normal.length_squared() <= 0.0001f) {
				shadow_normal = visual_transform.basis.get_column(1);
			}
			shadow_normal = shadow_normal.normalized();
			SimTransform shadow_transform = visual_transform * render_shadow_local_transforms[archetype];
			if (ground_distance >= 20.0f) {
				shadow_transform.basis.c0 = SimVec3();
				shadow_transform.basis.c1 = SimVec3();
				shadow_transform.basis.c2 = SimVec3();
			} else {
				shadow_transform.origin += -shadow_normal * ground_distance;
				shadow_transform.basis.c0 = shadow_transform.basis.c0.slide(shadow_normal);
				shadow_transform.basis.c1 = shadow_transform.basis.c1.slide(shadow_normal);
				shadow_transform.basis.c2 = shadow_transform.basis.c2.slide(shadow_normal);
			}
			render_shadow_multimeshes[archetype]->set_instance_transform(visible_slot, gd_transform(shadow_transform));
		}
	}
	for (int archetype = 0; archetype < static_cast<int>(render_car_multimeshes.size()); ++archetype) {
		const int visible_count = archetype < static_cast<int>(render_visible_counts.size()) ? render_visible_counts[archetype] : 0;
		render_last_body_instances += visible_count;
		if (render_car_multimeshes[archetype].is_valid()) {
			render_car_multimeshes[archetype]->set_visible_instance_count(visible_count);
		}
		if (archetype < static_cast<int>(render_outline_multimeshes.size()) && render_outline_multimeshes[archetype].is_valid()) {
			render_outline_multimeshes[archetype]->set_visible_instance_count(visible_count);
		}
		if (archetype < static_cast<int>(render_outline_main_multimeshes.size()) && render_outline_main_multimeshes[archetype].is_valid()) {
			render_outline_main_multimeshes[archetype]->set_visible_instance_count(visible_count);
		}
		if (archetype < static_cast<int>(render_shadow_multimeshes.size()) && render_shadow_multimeshes[archetype].is_valid()) {
			render_shadow_multimeshes[archetype]->set_visible_instance_count(visible_count);
		}
		if (archetype < static_cast<int>(render_stamp_multimeshes.size()) && render_stamp_multimeshes[archetype].is_valid()) {
			render_stamp_multimeshes[archetype]->set_visible_instance_count(visible_count);
		}
		if (archetype < static_cast<int>(render_thruster_multimeshes.size()) && render_thruster_multimeshes[archetype].is_valid()) {
			int thruster_count = 0;
			if (archetype < static_cast<int>(render_thruster_local_transforms.size())) {
				thruster_count = static_cast<int>(render_thruster_local_transforms[archetype].size());
			}
			const int visible_thruster_count = archetype < static_cast<int>(render_visible_thruster_counts.size()) ? render_visible_thruster_counts[archetype] : 0;
			const int thruster_instances = visible_thruster_count * thruster_count;
			render_last_thruster_instances += thruster_instances;
			render_thruster_multimeshes[archetype]->set_visible_instance_count(thruster_instances);
		}
	}
}

void GameSim::update_native_visual_effects(int visual_count, float alpha, bool step_effects, float effect_delta, bool step_electricity)
{
	if (!cars || render_vehicle_effect_refs.empty()) {
		return;
	}
	const int count = std::min(visual_count, static_cast<int>(render_vehicle_effect_refs.size()));
	if (static_cast<int>(render_thruster_current_thrust.size()) < count) {
		render_thruster_current_thrust.resize(count, 0.0f);
	}
	int local_car_index = -1;
	for (int i = 0; i < num_cars; ++i) {
		if (car_player_ids && car_player_ids[i] == gameplay_camera_player_id) {
			local_car_index = i;
			break;
		}
	}
	if (static_cast<int>(render_effect_full_flags.size()) < count) {
		render_effect_full_flags.resize(count);
	}
	std::fill(render_effect_full_flags.begin(), render_effect_full_flags.begin() + count, 0);
	constexpr int FULL_EFFECT_BUDGET = 30;
	float nearest_distances[FULL_EFFECT_BUDGET];
	int nearest_indices[FULL_EFFECT_BUDGET];
	for (int i = 0; i < FULL_EFFECT_BUDGET; ++i) {
		nearest_distances[i] = FLT_MAX;
		nearest_indices[i] = -1;
	}
	SimVec3 camera_origin;
	bool has_camera = false;
	if (gameplay_camera_node) {
		camera_origin = sim_vec3(gameplay_camera_node->get_global_transform().origin);
		has_camera = true;
	}
	for (int i = 0; i < count; ++i) {
		const SimTransform visual_transform = interpolate_sim_transform(
			render_final_prev_transforms[i],
			render_final_current_transforms[i],
			alpha);
		if (has_camera && !gameplay_camera_node->is_position_in_frustum(gd_vec3(visual_transform.origin))) {
			continue;
		}
		const float dist_sq = has_camera ? (visual_transform.origin - camera_origin).length_squared() : 0.0f;
		if (dist_sq < nearest_distances[FULL_EFFECT_BUDGET - 1]) {
			int insert_at = FULL_EFFECT_BUDGET - 1;
			while (insert_at > 0 && dist_sq < nearest_distances[insert_at - 1]) {
				nearest_distances[insert_at] = nearest_distances[insert_at - 1];
				nearest_indices[insert_at] = nearest_indices[insert_at - 1];
				--insert_at;
			}
			nearest_distances[insert_at] = dist_sq;
			nearest_indices[insert_at] = i;
		}
	}
	const int full_budget = std::min(count, FULL_EFFECT_BUDGET);
	if (local_car_index >= 0 && local_car_index < count) {
		bool local_selected = false;
		for (int n = 0; n < full_budget; ++n) {
			if (nearest_indices[n] == local_car_index) {
				local_selected = true;
				break;
			}
		}
		if (!local_selected && full_budget > 0) {
			nearest_indices[full_budget - 1] = local_car_index;
			nearest_distances[full_budget - 1] = -1.0f;
		}
	}
	for (int n = 0; n < full_budget; ++n) {
		const int idx = nearest_indices[n];
		if (idx >= 0 && idx < count) {
			render_effect_full_flags[idx] = 1;
		}
	}

	int max_thrusters_per_car = 0;
	if (render_thruster_lights_enabled) {
		for (const std::vector<SimTransform>& thrusters : render_thruster_local_transforms) {
			max_thrusters_per_car = std::max(max_thrusters_per_car, static_cast<int>(thrusters.size()));
		}
	}
	ensure_render_thruster_light_capacity((FULL_EFFECT_BUDGET + 1) * max_thrusters_per_car);
	RenderingServer* rs = RenderingServer::get_singleton();
	const float light_phase = std::sin(static_cast<float>(tick) * 2.0f) * 0.5f + 0.5f;
	int thruster_light_slot = 0;
	int node_effect_slot = 0;
	RenderEffectPoolSlot* local_effect_slot = nullptr;
	for (RenderEffectPoolSlot& slot : render_effect_pool_slots) {
		if (slot.fixed_local) {
			local_effect_slot = &slot;
			if (render_node_effects_enabled) {
				continue;
			}
		}
		if (!render_node_effects_enabled ||
				(slot.car_index >= 0 && (slot.car_index >= count || render_effect_full_flags[slot.car_index] == 0))) {
			if (slot.recharge_particles) {
				slot.recharge_particles->set_emitting(false);
			}
			if (slot.attack_particles) {
				slot.attack_particles->set_emitting(false);
			}
			if (slot.landing_particles) {
				slot.landing_particles->set_emitting(false);
			}
			if (slot.damage_electricity) {
				slot.damage_electricity->set_visible(false);
				slot.damage_electricity->set_emitting(false);
				slot.damage_electricity->set_amount_ratio(0.0);
			}
			if (slot.damage_smoke) {
				slot.damage_smoke->set_visible(false);
				slot.damage_smoke->set_emitting(false);
				slot.damage_smoke->set_amount_ratio(0.0);
			}
			if (slot.boost_electricity) {
				slot.boost_electricity->set("boosting", false);
				slot.boost_electricity->set("visible", false);
			}
			slot.car_index = -1;
		}
	}

	for (int i = 0; i < count; ++i) {
		RenderVehicleEffectRefs& refs = render_vehicle_effect_refs[i];
		PhysicsCarSoA& soa = *cars[i].soa;
		const int lane = cars[i].soa_index;
		const uint32_t machine_state = soa.machine_state[lane];
		const uint32_t terrain_state = soa.terrain_state[lane];
		SimTransform visual_transform = interpolate_sim_transform(
			render_final_prev_transforms[i],
			render_final_current_transforms[i],
			alpha);
		const bool full = render_effect_full_flags[i] != 0;

		if (step_effects && (machine_state & (MACHINESTATE::JUST_PRESSED_BOOST | MACHINESTATE::JUST_HIT_DASHPLATE)) != 0u) {
			refs.overlay.r += 0.293f * 0.75f;
			refs.overlay.g += 0.560f * 0.75f;
			refs.overlay.b += 0.886f * 0.75f;
		}
		if (step_effects && (machine_state & (MACHINESTATE::SPINATTACKING | MACHINESTATE::SIDEATTACKING)) != 0u) {
			refs.overlay.r += (0.5f - refs.overlay.r) * 0.5f;
			refs.overlay.g += (0.5f - refs.overlay.g) * 0.5f;
			refs.overlay.b += (0.0f - refs.overlay.b) * 0.5f;
		}
		if (step_effects && soa.s_boost_active[lane]) {
			refs.overlay.r += (1.0f - refs.overlay.r) * 0.6f;
			refs.overlay.g += (0.9f - refs.overlay.g) * 0.6f;
			refs.overlay.b += (0.3f - refs.overlay.b) * 0.6f;
		}
		if (step_effects && (terrain_state & TERRAIN::RECHARGE) != 0u) {
			refs.overlay.r += 0.018f;
			refs.overlay.b += 0.018f;
		}
		if (step_effects) {
			refs.overlay.r += (0.0f - refs.overlay.r) * 0.03f;
			refs.overlay.g += (0.0f - refs.overlay.g) * 0.03f;
			refs.overlay.b += (0.0f - refs.overlay.b) * 0.03f;
		}
		const float max_energy = std::max(soa.calced_max_energy[lane], 0.001f);
		const float energy_ratio = std::clamp(soa.energy[lane] / max_energy, 0.0f, 1.0f);
		const float health_effect_ratio = std::min(1.0f, energy_ratio * 4.0f);
		const float low_energy_ratio = std::max(0.0f, 1.0f - health_effect_ratio);
		const float boost_frames = static_cast<float>(std::max(soa.boost_frames[lane], soa.boost_frames_manual[lane]));
		const float boost_duration_frames = std::max(1.0f, soa.stat_boost_length[lane] * 60.0f);
		const float boost_ratio = boost_frames / boost_duration_frames;
		const float low_energy_flash = (std::sin(static_cast<float>(tick) * 0.25f) * 0.5f + 0.5f) * low_energy_ratio;
		refs.energy_overlay = godot::Color(0.8f * low_energy_flash, -0.2f * low_energy_flash, -0.2f * low_energy_flash, 1.0f);
		const float thrust = std::max(0.0f, (soa.input_accel[lane] + std::sqrt(std::max(0.0f, soa.boost_turbo[lane])) * 0.1f) * soa.input_accel[lane]);
		render_thruster_current_thrust[i] += (thrust - render_thruster_current_thrust[i]) * 0.4f;
		if (!full) {
			if (refs.full_effect_active) {
				refs.full_effect_active = 0;
			}
			if (step_effects) {
				refs.terrain_state_old = terrain_state;
				refs.machine_state_old = machine_state;
			}
			continue;
		}
		refs.full_effect_active = 1;
		RenderEffectPoolSlot* pool_slot = nullptr;
		if (!render_node_effects_enabled) {
			pool_slot = nullptr;
		} else if (i == local_car_index && local_effect_slot) {
			pool_slot = local_effect_slot;
		} else {
			while (node_effect_slot < static_cast<int>(render_effect_pool_slots.size()) &&
					render_effect_pool_slots[node_effect_slot].fixed_local) {
				++node_effect_slot;
			}
			if (node_effect_slot < static_cast<int>(render_effect_pool_slots.size())) {
				pool_slot = &render_effect_pool_slots[node_effect_slot];
				++node_effect_slot;
			}
		}
		if (pool_slot) {
			if (pool_slot->car_index != i) {
				if (pool_slot->recharge_particles) {
					pool_slot->recharge_particles->set_emitting(false);
				}
				if (pool_slot->attack_particles) {
					pool_slot->attack_particles->set_emitting(false);
				}
				if (pool_slot->landing_particles) {
					pool_slot->landing_particles->set_emitting(false);
				}
				if (pool_slot->damage_electricity) {
					pool_slot->damage_electricity->set_visible(false);
					pool_slot->damage_electricity->set_emitting(false);
					pool_slot->damage_electricity->set_amount_ratio(0.0);
					pool_slot->damage_electricity->restart();
				}
				if (pool_slot->damage_smoke) {
					pool_slot->damage_smoke->set_emitting(false);
					pool_slot->damage_smoke->set_amount_ratio(0.0);
					pool_slot->damage_smoke->restart();
				}
				if (pool_slot->boost_electricity) {
					pool_slot->boost_electricity->set("boosting", false);
					pool_slot->boost_electricity->set("visible", false);
					pool_slot->boost_electricity->set("old_transform", gd_transform(visual_transform));
					pool_slot->boost_electricity->set("queued_tendrils", 0.0);
				}
				if (!pool_slot->fixed_local && pool_slot->node) {
					pool_slot->node->set("owning_id", car_player_ids ? car_player_ids[i] : -1);
				}
			}
			pool_slot->car_index = i;
		}
		if (pool_slot) {
			if (pool_slot->car_transform) {
				pool_slot->car_transform->set_global_transform(gd_transform(visual_transform));
			}
			if (pool_slot->recharge_particles) {
				pool_slot->recharge_particles->set_emitting((terrain_state & TERRAIN::RECHARGE) != 0u);
			}
			if (pool_slot->attack_particles) {
				pool_slot->attack_particles->set_emitting((machine_state & (MACHINESTATE::SIDEATTACKING | MACHINESTATE::SPINATTACKING)) != 0u);
			}
			if (step_effects && pool_slot->landing_particles &&
					(machine_state & MACHINESTATE::JUSTLANDED) != 0u &&
					(refs.machine_state_old & MACHINESTATE::JUSTLANDED) == 0u) {
				pool_slot->landing_particles->restart();
				pool_slot->landing_particles->set_emitting(true);
			}
			if (pool_slot->damage_electricity) {
				const bool active = low_energy_ratio > 0.001f || boost_ratio > 0.5f;
				const SimVec3 damage_effect_origin = visual_transform.origin + visual_transform.basis.get_column(1) * -0.125f;
				pool_slot->damage_electricity->set_global_position(gd_vec3(damage_effect_origin));
				pool_slot->damage_electricity->set_visible(active);
				pool_slot->damage_electricity->set_emitting(active);
				pool_slot->damage_electricity->set_amount_ratio(active ? low_energy_ratio + std::max(boost_ratio - 0.5f, 0.0f) : 0.0f);
				if (pool_slot->damage_electricity_material.is_valid()) {
					pool_slot->damage_electricity_material->set(
						StringName("color"),
						godot::Color(
							1.0f + (0.5f - 1.0f) * health_effect_ratio,
							0.75f,
							0.5f + (1.0f - 0.5f) * health_effect_ratio,
							1.0f));
				}
			}
			if (pool_slot->damage_smoke) {
				const bool smoke_active = low_energy_ratio > 0.001f;
				pool_slot->damage_smoke->set_visible(smoke_active);
				pool_slot->damage_smoke->set_emitting(smoke_active);
				pool_slot->damage_smoke->set_amount_ratio(smoke_active ? low_energy_ratio : 0.0f);
			}
		}
		const int archetype = i < static_cast<int>(render_car_archetype_indices.size()) ? render_car_archetype_indices[i] : -1;
		if (render_thruster_lights_enabled && rs && archetype >= 0 && archetype < static_cast<int>(render_thruster_local_transforms.size())) {
			const std::vector<SimTransform>& local_thrusters = render_thruster_local_transforms[archetype];
			const float current_thrust = render_thruster_current_thrust[i];
			for (int t = 0; t < static_cast<int>(local_thrusters.size()) && thruster_light_slot < static_cast<int>(render_thruster_lights.size()); ++t) {
				RenderThrusterLightRID& light = render_thruster_lights[thruster_light_slot];
				if (current_thrust > 0.01f) {
					const SimTransform thruster_transform = visual_transform * local_thrusters[t];
					rs->instance_set_transform(light.instance, gd_transform(thruster_transform));
					rs->light_set_param(light.light, RenderingServer::LIGHT_PARAM_ENERGY, (4.0f + 2.0f * light_phase) * current_thrust);
					rs->light_set_param(light.light, RenderingServer::LIGHT_PARAM_ATTENUATION, (1.0f + 2.0f * light_phase) * current_thrust);
					rs->instance_set_visible(light.instance, true);
				} else {
					rs->instance_set_visible(light.instance, false);
				}
				++thruster_light_slot;
			}
		}

		if (pool_slot && pool_slot->boost_electricity) {
			SimVec3 track_up = LOAD_INDEXED_VEC3(soa, track_surface_normal, lane);
			if (track_up.length_squared() <= 0.0001f) {
				track_up = visual_transform.basis.get_column(1);
			}
			track_up = track_up.normalized();
			float ground_distance = 20.0f - soa.height_above_track[lane];
			if (ground_distance < 0.0f) {
				ground_distance = 0.0f;
			}
			if (ground_distance > 20.0f) {
				ground_distance = 20.0f;
			}
			const SimVec3 track_surface_pos = LOAD_INDEXED_VEC3(soa, position_current, lane) - track_up * ground_distance;
			const bool boosting =
				full &&
				((soa.boost_frames[lane] > 0u || soa.boost_frames_manual[lane] > 0u) &&
					(machine_state & MACHINESTATE::AIRBORNE) == 0u);
			pool_slot->boost_electricity->set("boosting", boosting);
			pool_slot->boost_electricity->set("visible", true);
			if (full && step_electricity) {
				pool_slot->boost_electricity->set("ground", godot::Plane(gd_vec3(track_up), gd_vec3(track_surface_pos)));
				pool_slot->boost_electricity->set("tendril_lifetime", std::max(0.1f, std::min(0.3f, 0.3f - soa.speed_kmh[lane] * (0.2f / 3000.0f))));
				pool_slot->boost_electricity->call("calculate_electricity", static_cast<double>(effect_delta), gd_transform(visual_transform));
			}
		}

		if (step_effects) {
			refs.terrain_state_old = terrain_state;
			refs.machine_state_old = machine_state;
		}
	}
	for (int i = node_effect_slot; i < static_cast<int>(render_effect_pool_slots.size()); ++i) {
		RenderEffectPoolSlot& slot = render_effect_pool_slots[i];
		if (slot.fixed_local) {
			continue;
		}
		if (slot.recharge_particles) {
			slot.recharge_particles->set_emitting(false);
		}
		if (slot.attack_particles) {
			slot.attack_particles->set_emitting(false);
		}
		if (slot.landing_particles) {
			slot.landing_particles->set_emitting(false);
		}
		if (slot.damage_electricity) {
			slot.damage_electricity->set_visible(false);
			slot.damage_electricity->set_emitting(false);
			slot.damage_electricity->set_amount_ratio(0.0);
		}
		if (slot.damage_smoke) {
			slot.damage_smoke->set_emitting(false);
			slot.damage_smoke->set_amount_ratio(0.0);
		}
		if (slot.boost_electricity) {
			slot.boost_electricity->set("boosting", false);
			slot.boost_electricity->set("visible", false);
		}
	}
	hide_unused_render_thruster_lights(thruster_light_slot);
}

void GameSim::update_native_gameplay_camera(bool step_camera)
{
	if (!gameplay_camera_node || gameplay_camera.is_null() || !cars || !car_player_ids) {
		return;
	}
	int car_index = -1;
	for (int i = 0; i < num_cars; ++i) {
		if (car_player_ids[i] == gameplay_camera_player_id) {
			car_index = i;
			break;
		}
	}
	if (car_index < 0) {
		return;
	}
	PhysicsCarSoA& soa = *cars[car_index].soa;
	const int lane = cars[car_index].soa_index;
	SimVec3 camera_position_correction;
	const bool has_camera_render_correction =
		car_index < static_cast<int>(render_rollback_correction_active.size()) &&
		render_rollback_correction_active[car_index] &&
		car_index < static_cast<int>(render_rollback_corrections.size());
	if (has_camera_render_correction) {
		camera_position_correction = render_rollback_corrections[car_index].origin;
	}
	if (step_camera) {
		float aspect_ratio = 4.0f / 3.0f;
		if (godot::Viewport* viewport = gameplay_camera_node->get_viewport()) {
			const godot::Vector2 size = viewport->get_visible_rect().size;
			if (size.y > 0.0f) {
				aspect_ratio = static_cast<float>(size.x / size.y);
			}
		}
		SimVec3 track_up = LOAD_INDEXED_VEC3(soa, track_surface_normal, lane);
		if (track_up.length_squared() <= 0.0001f) {
			track_up = MXT_LOAD_TRANSFORM(soa, basis_physical, lane).basis.get_column(1);
		}
		track_up = track_up.normalized();
		godot::Input* input = godot::Input::get_singleton();
		const bool view_up_pressed = input && input->is_action_just_pressed(godot::StringName("CameraUp"));
		const bool view_down_pressed = input && input->is_action_just_pressed(godot::StringName("CameraDown"));
			gameplay_camera->step(
			gd_vec3(LOAD_INDEXED_VEC3(soa, position_current, lane) + camera_position_correction),
			gd_vec3(LOAD_INDEXED_VEC3(soa, position_old, lane) + camera_position_correction),
			gd_transform(MXT_LOAD_TRANSFORM(soa, basis_physical, lane)),
			gd_vec3(track_up),
			gd_vec3(LOAD_INDEXED_VEC3(soa, track_surface_pos, lane)),
			soa.height_above_track[lane],
			soa.speed_kmh[lane],
			soa.camera_reorienting[lane],
			soa.camera_repositioning[lane],
				car_index < static_cast<int>(render_vehicle_visual_state.size()) ? render_vehicle_visual_state[car_index].turn_reaction_effect : 0.0f,
			static_cast<int>(soa.machine_state[lane]),
			static_cast<int>(soa.state_2[lane]),
			static_cast<int>(soa.tilt_state[lane * 4 + 0]),
			static_cast<int>(soa.tilt_state[lane * 4 + 1]),
			static_cast<int>(soa.tilt_state[lane * 4 + 2]),
			static_cast<int>(soa.tilt_state[lane * 4 + 3]),
			static_cast<int>(soa.restore_state[lane]),
			static_cast<int>(soa.restore_move_frames[lane]),
			aspect_ratio,
			view_up_pressed,
			view_down_pressed);
	}
	Engine* engine = Engine::get_singleton();
	const float alpha = engine ? static_cast<float>(engine->get_physics_interpolation_fraction()) : 1.0f;
	auto get_interpolated_car_transform = [&](int index) -> SimTransform {
		if (index >= 0 &&
				index < static_cast<int>(render_final_prev_transforms.size()) &&
				index < static_cast<int>(render_final_current_transforms.size())) {
			return interpolate_sim_transform(
				render_final_prev_transforms[index],
				render_final_current_transforms[index],
				alpha);
		}
		PhysicsCarSoA& fallback_soa = *cars[index].soa;
		const int fallback_lane = cars[index].soa_index;
		SimTransform fallback = interpolate_sim_transform(
			MXT_LOAD_TRANSFORM(fallback_soa, basis_physical_other, fallback_lane),
			MXT_LOAD_TRANSFORM(fallback_soa, basis_physical, fallback_lane),
			alpha);
		fallback.origin = LOAD_INDEXED_VEC3(fallback_soa, position_old, fallback_lane).lerp(
			LOAD_INDEXED_VEC3(fallback_soa, position_current, fallback_lane),
			alpha);
		return fallback;
	};
	godot::Transform3D render_transform = gameplay_camera->get_render_transform(alpha);
	float render_fov = gameplay_camera->get_render_fov(alpha);
	bool intro_camera_active = false;
	if (multiplayer_intro_camera_enabled && start_countdown_extra_frames > 0u) {
		const float intro_frame = static_cast<float>(tick) + alpha;
		if (intro_frame < static_cast<float>(start_countdown_extra_frames)) {
			intro_camera_active = true;
			constexpr float kFlybyFrames = 420.0f;
			constexpr float kReturnFrames = 180.0f;
			SimTransform focus_basis_transform = get_interpolated_car_transform(car_index);
			SimVec3 up = LOAD_INDEXED_VEC3(soa, track_surface_normal, lane);
			if (up.length_squared() <= 0.0001f) {
				up = focus_basis_transform.basis.get_column(1);
			}
			up = up.normalized();
			SimVec3 forward = focus_basis_transform.basis.get_column(2) * -1.0f;
			if (forward.length_squared() <= 0.0001f) {
				forward = SimVec3(0.0f, 0.0f, -1.0f);
			}
			forward = forward.normalized();
			SimVec3 right = focus_basis_transform.basis.get_column(0);
			if (right.length_squared() <= 0.0001f) {
				right = forward.cross(up);
			}
			right = right.normalized();
			const SimVec3 origin = focus_basis_transform.origin + camera_position_correction;
			float min_forward = FLT_MAX;
			float max_forward = -FLT_MAX;
			float lateral_sum = 0.0f;
			int grid_count = 0;
			for (int i = 0; i < num_cars; ++i) {
				if (car_player_ids && car_player_ids[i] < 0) {
					continue;
				}
				const SimVec3 pos = get_interpolated_car_transform(i).origin;
				const SimVec3 delta = pos - origin;
				const float forward_proj = delta.dot(forward);
				min_forward = std::min(min_forward, forward_proj);
				max_forward = std::max(max_forward, forward_proj);
				lateral_sum += delta.dot(right);
				grid_count += 1;
			}
			if (grid_count > 0) {
				const float flyby_alpha = smoothstep01(std::min(intro_frame / kFlybyFrames, 1.0f));
				const float sweep_start = min_forward - 24.0f;
				const float sweep_end = max_forward + 24.0f;
				const float sweep = sweep_start + (sweep_end - sweep_start) * flyby_alpha;
				const float lateral = lateral_sum / static_cast<float>(grid_count);
				const SimVec3 interest_sim = origin + forward * sweep + right * lateral + up * 3.5f;
				const SimVec3 position_sim = interest_sim - forward * 58.0f + right * 42.0f + up * 30.0f;
				const godot::Vector3 preview_interest = gd_vec3(interest_sim);
				const godot::Vector3 preview_position = gd_vec3(position_sim);
				const godot::Vector3 preview_up = gd_vec3(up);
				if (intro_frame < kFlybyFrames) {
					render_transform = build_camera_transform(preview_position, preview_interest, preview_up);
					render_fov = 72.0f;
				} else {
					const float return_alpha = smoothstep01((intro_frame - kFlybyFrames) / kReturnFrames);
					const godot::Vector3 normal_position = render_transform.origin;
					const godot::Vector3 normal_up = render_transform.basis.get_column(1).normalized();
					const godot::Vector3 normal_interest = normal_position - render_transform.basis.get_column(2).normalized() * 80.0f;
					const godot::Vector3 blended_position = preview_position.lerp(normal_position, return_alpha);
					const godot::Vector3 blended_interest = preview_interest.lerp(normal_interest, return_alpha);
					const godot::Vector3 blended_up = preview_up.lerp(normal_up, return_alpha).normalized();
					render_transform = build_camera_transform(blended_position, blended_interest, blended_up);
					render_fov = 72.0f + (render_fov - 72.0f) * return_alpha;
				}
			}
		}
	}
	if (!intro_camera_active && (soa.machine_state[lane] & MACHINESTATE::COMPLETEDRACE_1_Q) != 0u) {
		SimTransform car_basis = get_interpolated_car_transform(car_index);
		SimVec3 up_sim = LOAD_INDEXED_VEC3(soa, track_surface_normal, lane);
		if (up_sim.length_squared() <= 0.0001f) {
			up_sim = car_basis.basis.get_column(1);
		}
		up_sim = up_sim.normalized();
		SimVec3 forward_sim = car_basis.basis.get_column(2) * -1.0f;
		if (forward_sim.length_squared() <= 0.0001f) {
			forward_sim = SimVec3(0.0f, 0.0f, -1.0f);
		}
		forward_sim = forward_sim.normalized();
		SimVec3 right_sim = car_basis.basis.get_column(0);
		if (right_sim.length_squared() <= 0.0001f) {
			right_sim = forward_sim.cross(up_sim);
		}
		right_sim = right_sim.normalized();
		const SimVec3 car_pos_sim = car_basis.origin;
		const godot::Vector3 car_pos = gd_vec3(car_pos_sim);
		const godot::Vector3 up = gd_vec3(up_sim);
		const godot::Vector3 forward = gd_vec3(forward_sim);
		const godot::Vector3 right = gd_vec3(right_sim);
		const float mode_time = static_cast<float>(tick) + alpha;
		const float mode_phase = std::fmod(mode_time, 240.0f) / 240.0f;
		const int camera_mode = static_cast<int>(std::floor(mode_time / 240.0f)) & 3;
		godot::Vector3 interest = car_pos + up * 2.4f;
		godot::Vector3 position;
		if (camera_mode == 0) {
			position = interest - forward * 24.0f + up * 8.0f + right * 9.0f;
			render_fov = 62.0f;
		} else if (camera_mode == 1) {
			const float side = mode_phase < 0.5f ? -1.0f : 1.0f;
			position = interest + forward * 34.0f + right * (24.0f * side) + up * 7.5f;
			render_fov = 54.0f;
		} else if (camera_mode == 2) {
			const float angle = mode_phase * 6.28318530718f;
			position = interest + right * (std::cos(angle) * 28.0f) - forward * (std::sin(angle) * 28.0f) + up * 10.0f;
			render_fov = 66.0f;
		} else {
			position = interest - forward * 10.0f + up * 38.0f + right * 7.0f;
			render_fov = 72.0f;
		}
		render_transform = build_camera_transform(position, interest, up);
	}
	gameplay_camera_node->set_global_transform(render_transform);
	gameplay_camera_node->set_fov(render_fov);
	gameplay_camera_node->set_near(0.25);
	gameplay_camera_node->set_far(40000.0);
}

void GameSim::render_gamesim_visuals_only(double process_delta)
{
	if (!sim_started || !cars) {
		return;
	}
	const uint64_t profile_start = render_profile_enabled ? render_profile_now_us() : 0;
	Engine* engine = Engine::get_singleton();
	const float alpha = engine ? static_cast<float>(engine->get_physics_interpolation_fraction()) : 1.0f;
	const float effect_delta = std::max(0.0f, std::min(0.1f, static_cast<float>(process_delta)));
	uint64_t profile_step = render_profile_enabled ? render_profile_now_us() : 0;
	update_native_visual_effects(std::min(num_cars, static_cast<int>(render_final_current_transforms.size())), alpha, false, effect_delta, true);
	if (render_profile_enabled) {
		const uint64_t now = render_profile_now_us();
		render_profile_visuals_only_effects_us += now - profile_step;
		profile_step = now;
	}
	apply_render_multimeshes(alpha);
	if (render_profile_enabled) {
		const uint64_t now = render_profile_now_us();
		render_profile_visuals_only_multimesh_us += now - profile_step;
		render_profile_visuals_only_body_instances += static_cast<uint64_t>(std::max(render_last_body_instances, 0));
		render_profile_visuals_only_thruster_instances += static_cast<uint64_t>(std::max(render_last_thruster_instances, 0));
		profile_step = now;
	}
	update_native_gameplay_camera(false);
	if (render_profile_enabled) {
		const uint64_t now = render_profile_now_us();
		render_profile_visuals_only_camera_us += now - profile_step;
		render_profile_visuals_only_total_us += now - profile_start;
		render_profile_visuals_only_frames += 1;
	}
}

void GameSim::set_player_metadata(godot::Array player_ids, godot::Array cpu_flags)
{
	if (!car_player_ids || !car_is_cpu) {
		return;
	}
	for (int i = 0; i < num_cars; ++i) {
		car_player_ids[i] = -1;
		car_is_cpu[i] = 0;
	}
	const int limit = std::min(num_cars, static_cast<int>(player_ids.size()));
	for (int i = 0; i < limit; ++i) {
		godot::Variant id_var = player_ids[i];
		int32_t pid = -1;
		if (id_var.get_type() == godot::Variant::INT) {
			pid = static_cast<int32_t>(id_var.operator int64_t());
		} else if (id_var.get_type() == godot::Variant::FLOAT) {
			pid = static_cast<int32_t>(id_var.operator double());
		}
		car_player_ids[i] = pid;
		bool is_cpu = false;
		if (i < cpu_flags.size()) {
			godot::Variant flag_var = cpu_flags[i];
			if (flag_var.get_type() == godot::Variant::BOOL) {
				is_cpu = static_cast<bool>(flag_var);
			} else if (flag_var.get_type() == godot::Variant::INT) {
				is_cpu = flag_var.operator int64_t() != 0;
			}
		}
		car_is_cpu[i] = is_cpu ? 1 : 0;
	}
	configure_native_cpu_drivers();
}

godot::PackedByteArray GameSim::build_cpu_observation(const PhysicsCar& car) const
{
	godot::Ref<godot::StreamPeerBuffer> buffer;
	buffer.instantiate();
	buffer->seek(0);
	auto write_vec3 = [&](const SimVec3& v) {
		buffer->put_float(v.x);
		buffer->put_float(v.y);
		buffer->put_float(v.z);
	};
	write_vec3(car.soa->road_sample[car.soa_index].spatial_t);
	buffer->put_float(car.soa->road_sample[car.soa_index].road_t.x);
	buffer->put_float(car.soa->road_sample[car.soa_index].road_t.y);
	write_vec3(LOAD_INDEXED_VEC3(*car.soa, position_current, car.soa_index));
	write_vec3(LOAD_INDEXED_VEC3(*car.soa, velocity, car.soa_index));
	write_vec3(LOAD_INDEXED_VEC3(*car.soa, velocity_angular, car.soa_index));
	const SimBasis basis = MXT_LOAD_TRANSFORM(*car.soa, basis_physical, car.soa_index).basis;
	for (int col = 0; col < 3; ++col) {
		write_vec3(basis.get_column(col));
	}
	write_vec3(MXT_LOAD_TRANSFORM(*car.soa, basis_physical, car.soa_index).origin);
	for (int col = 0; col < 3; ++col) {
		write_vec3(car.soa->road_sample[car.soa_index].closest_surface.basis.get_column(col));
	}
	buffer->put_float(car.soa->base_speed[car.soa_index]);
	buffer->put_float(car.soa->energy[car.soa_index]);
	buffer->put_float(car.soa->checkpoint_fraction[car.soa_index]);
	buffer->put_u16(car.soa->current_checkpoint[car.soa_index]);
	buffer->put_u32(car.soa->terrain_state[car.soa_index]);
	buffer->put_u32(car.soa->machine_state[car.soa_index]);
	buffer->put_u8(car.soa->restore_state[car.soa_index]);
	buffer->put_u32(car.soa->tilt_state[car.soa_index * 4 + 1]);
	return buffer->get_data_array();
}

void GameSim::reset_super_sparks()
{
	if (!super_spark_state || !super_sparks) {
		return;
	}
	super_spark_state->cursor = 0;
	super_spark_state->placement_timer = 0;
	super_spark_state->rng_state = 1;
	for (int i = 0; i < SUPER_SPARK_CAPACITY; ++i) {
		super_sparks[i].active = 0;
		super_sparks[i].collectable = 0;
		super_sparks[i].animation_frame = 0;
		super_sparks[i].checkpoint = 0;
		super_sparks[i].position = SimVec3();
		super_sparks[i].prev_position = SimVec3();
		super_sparks[i].start_position = SimVec3();
		super_sparks[i].final_position = SimVec3();
		super_sparks[i].plane_normal = SimVec3(0.0f, 1.0f, 0.0f);
	}
}

uint16_t GameSim::compute_s_boost_duration_frames(float gap_distance) const
{
	float seconds = 3.0f;
	if (gap_distance <= 1000.0f) {
		seconds = 3.0f;
	} else if (gap_distance >= 10000.0f) {
		seconds = 8.0f;
	} else {
		float t = (gap_distance - 1000.0f) / 9000.0f;
		seconds = 3.0f + t * 5.0f;
	}
	uint16_t frames = static_cast<uint16_t>(seconds * 60.0f + 0.5f);
	if (frames < 180u)
		frames = 180u;
	return frames;
}

float GameSim::compute_car_distance_along_track(const PhysicsCar& car) const
{
	return compute_vehicle_distance_along_track(car.soa->current_checkpoint[car.soa_index], car.soa->checkpoint_fraction[car.soa_index], car.soa->lap[car.soa_index]);
}

float GameSim::compute_vehicle_distance_along_track(uint16_t current_checkpoint, float checkpoint_fraction, uint8_t lap) const
{
	if (!current_track)
		return 0.0f;
	return current_track->compute_lap_distance(current_checkpoint, checkpoint_fraction, lap);
}

void GameSim::emit_super_sparks_from_car(const PhysicsCar& car, int count)
{
	if (count <= 0)
		return;
	if (!s_boost_enabled)
		return;
	if (!sim_started || !super_spark_state || !super_sparks || !current_track)
		return;
	if ((car.soa->machine_state[car.soa_index] & MACHINESTATE::STARTINGCOUNTDOWN) != 0)
		return;
	if ((car.soa->machine_state[car.soa_index] & (MACHINESTATE::AIRBORNE | MACHINESTATE::ZEROHP)) != 0)
		return;
	if (car.soa->restore_state[car.soa_index] == 2)
		return;

	constexpr uint64_t kPostCountdownBlockFrames = 180;
	const uint64_t current_frame = static_cast<uint64_t>(car.soa->simulation_tick[car.soa_index]);
	const uint64_t safe_frame = static_cast<uint64_t>(car.soa->level_start_time[car.soa_index]) + kPostCountdownBlockFrames;
	if (current_frame < safe_frame)
		return;

	const uint16_t checkpoint = car.soa->current_checkpoint[car.soa_index];
	if (checkpoint >= static_cast<uint16_t>(current_track->num_checkpoints))
		return;
	const SimVec3 car_position = LOAD_INDEXED_VEC3(*car.soa, position_current, car.soa_index);
	SimVec3 normal_in = LOAD_INDEXED_VEC3(*car.soa, track_surface_normal, car.soa_index);
	if (normal_in.length_squared() <= 0.0001f) {
		normal_in = SimVec3(0.0f, 1.0f, 0.0f);
	} else {
		normal_in = normal_in.normalized();
	}
	SimVec3 tangent_a = car.soa->road_sample[car.soa_index].closest_surface.basis.get_column(0);
	if (tangent_a.length_squared() < 0.0001f) {
		tangent_a = normal_in.cross(SimVec3(0.0f, 0.0f, 1.0f));
	}
	if (tangent_a.length_squared() < 0.0001f) {
		tangent_a = normal_in.cross(SimVec3(1.0f, 0.0f, 0.0f));
	}
	tangent_a = tangent_a.slide(normal_in).normalized();
	SimVec3 tangent_b = normal_in.cross(tangent_a).normalized();

	auto next_rand = [&]() -> float {
		super_spark_state->rng_state = super_spark_state->rng_state * 1664525u + 1013904223u;
		return static_cast<float>(super_spark_state->rng_state & 0x00FFFFFFu) / 16777215.0f;
	};
	auto rand_range = [&](float min_v, float max_v) -> float {
		return min_v + (max_v - min_v) * next_rand();
	};

	for (int n = 0; n < count; ++n) {
		const uint16_t cursor = super_spark_state->cursor;
		SuperSpark& spark = super_sparks[cursor];
		super_spark_state->cursor = static_cast<uint16_t>((cursor + 1) % SUPER_SPARK_CAPACITY);

		const float lateral_a = rand_range(-18.0f, 18.0f);
		const float lateral_b = rand_range(-10.0f, 10.0f);
		const SimVec3 sample_point = car_position + tangent_a * lateral_a + tangent_b * lateral_b;
		SimVec2 road_t;
		SimVec3 spatial_t;
		SimTransform surface;
		current_track->get_road_surface(checkpoint, sample_point, road_t, spatial_t, surface, true);
		SimVec3 surface_normal = surface.basis.get_column(1);
		if (surface_normal.length_squared() <= 0.0001f) {
			surface_normal = normal_in;
		} else {
			surface_normal = surface_normal.normalized();
		}
		const SimVec3 final_position = surface.origin + surface_normal * 1.0f;

		spark.active = 1;
		spark.collectable = 0;
		spark.animation_frame = 0;
		spark.checkpoint = checkpoint;
		spark.plane_normal = surface_normal;
		spark.start_position = car_position;
		spark.final_position = final_position;
		spark.position = car_position;
		spark.prev_position = car_position;
	}
}

static constexpr uint16_t MXT_SUPER_SPARK_ANIMATION_FRAMES = 30;
static constexpr float MXT_SUPER_SPARK_ARC_HEIGHT = 8.4f;

static SimVec3 mxt_super_spark_position_at_frame(const SimVec3& start_position, const SimVec3& final_position, const SimVec3& plane_normal, uint16_t animation_frame, uint8_t collectable)
{
	if (collectable) {
		return final_position;
	}
	const float t = std::min(static_cast<float>(animation_frame) / static_cast<float>(MXT_SUPER_SPARK_ANIMATION_FRAMES), 1.0f);
	const float arc = 4.0f * t * (1.0f - t);
	return start_position.lerp(final_position, t) + plane_normal * (MXT_SUPER_SPARK_ARC_HEIGHT * arc);
}

void GameSim::update_super_sparks()
{
	if (!s_boost_enabled) {
		return;
	}
	if (!sim_started || !cars || !super_spark_state || !super_sparks)
		return;

	const float collect_radius_sq = SUPER_SPARK_COLLECT_RADIUS * SUPER_SPARK_COLLECT_RADIUS;
	auto checkpoint_matches = [&](uint16_t spark_checkpoint, uint16_t car_checkpoint) -> bool {
		if (!current_track || car_checkpoint >= static_cast<uint16_t>(current_track->num_checkpoints))
			return false;
		if (spark_checkpoint == car_checkpoint)
			return true;
		const CollisionCheckpoint& cp = current_track->checkpoints[car_checkpoint];
		for (int n = 0; n < cp.num_neighboring_checkpoints; ++n) {
			if (cp.neighboring_checkpoints && cp.neighboring_checkpoints[n] == spark_checkpoint) {
				return true;
			}
		}
		return false;
	};

	for (int i = 0; i < SUPER_SPARK_CAPACITY; ++i) {
		SuperSpark& spark = super_sparks[i];
		if (!spark.active)
			continue;
		spark.prev_position = spark.position;

		if (!spark.collectable) {
			spark.position = mxt_super_spark_position_at_frame(
				spark.start_position, spark.final_position, spark.plane_normal, spark.animation_frame, spark.collectable);
			if (spark.animation_frame >= MXT_SUPER_SPARK_ANIMATION_FRAMES) {
				spark.position = spark.final_position;
				spark.collectable = 1;
			} else {
				spark.animation_frame += 1;
				continue;
			}
		}

		for (int car_idx = 0; car_idx < num_cars; ++car_idx) {
			PhysicsCarSoA& car_soa = *cars[car_idx].soa;
			const int lane = cars[car_idx].soa_index;
			if (car_soa.s_boost_active[lane] || (car_soa.machine_state[lane] & MACHINESTATE::ZEROHP) != 0)
				continue;
			if (!checkpoint_matches(spark.checkpoint, car_soa.current_checkpoint[lane]))
				continue;
			SimVec3 closest = get_closest_point_to_segment(
				spark.position, LOAD_INDEXED_VEC3(car_soa, position_old, lane), LOAD_INDEXED_VEC3(car_soa, position_current, lane));
			float dist_sq = spark.position.distance_squared_to(closest);
			if (dist_sq <= collect_radius_sq) {
				if (car_soa.s_boost_charge[lane] < car_soa.s_boost_charge_max[lane]) {
					car_soa.s_boost_charge[lane] += 1;
				}
				car_soa.base_speed[lane] += 0.05f;
				spark.active = 0;
				spark.collectable = 0;
				break;
			}
		}
	}
}

void GameSim::update_super_spark_visuals()
{
	if (!spark_node_container || !super_sparks)
		return;
	if (!spark_multimesh_instance) {
		Node *spark_node = spark_node_container->get_node_or_null(NodePath("SparkMultiMesh"));
		spark_multimesh_instance = Object::cast_to<godot::MultiMeshInstance3D>(spark_node);
		if (!spark_multimesh_instance) {
			return;
		}
	}
	Ref<godot::MultiMesh> spark_multimesh = spark_multimesh_instance->get_multimesh();
	if (spark_multimesh.is_null()) {
		return;
	}
	Engine* engine = Engine::get_singleton();
	const float alpha = engine ? static_cast<float>(engine->get_physics_interpolation_fraction()) : 1.0f;
	int active_count = 0;
	for (int i = 0; i < SUPER_SPARK_CAPACITY; ++i) {
		if (super_sparks[i].active == 0) {
			continue;
		}
		godot::Transform3D spark_transform;
		const SimVec3 render_position = super_sparks[i].prev_position.lerp(super_sparks[i].position, alpha);
		spark_transform.origin = gd_vec3(render_position);
		spark_multimesh->set_instance_transform(active_count, spark_transform);
		active_count += 1;
	}
	spark_multimesh->set_visible_instance_count(active_count);
}

	void GameSim::render_gamesim() {
		if (!sim_started || !car_node_container || !cars) {
			return;
		}

		if (car_node_container == nullptr) {
			return;
		}

		const uint64_t profile_start = render_profile_enabled ? render_profile_now_us() : 0;
		uint64_t profile_step = profile_start;
		TypedArray<godot::Node> vis_cars = car_node_container->get_children();
		if (render_profile_enabled) {
			const uint64_t now = render_profile_now_us();
			render_profile_get_children_us += now - profile_step;
			profile_step = now;
		}
		const int vis_car_count = std::max(0, num_cars);
		const int native_visual_count = std::max(vis_car_count + (bumpers_enabled ? bumper_count : 0),
				static_cast<int>(render_car_archetype_indices.size()));
		if (static_cast<int>(render_vehicle_effect_refs.size()) != vis_car_count ||
				render_effect_pool_slots.empty()) {
			cache_native_visual_effect_nodes();
		}
		if (render_profile_enabled) {
			const uint64_t now = render_profile_now_us();
			render_profile_cache_us += now - profile_step;
			profile_step = now;
		}
		update_render_visual_snapshots(native_visual_count);
		if (render_profile_enabled) {
			const uint64_t now = render_profile_now_us();
			render_profile_snapshots_us += now - profile_step;
			profile_step = now;
		}
		Engine* engine = Engine::get_singleton();
		const float alpha = engine ? static_cast<float>(engine->get_physics_interpolation_fraction()) : 1.0f;
		update_native_visual_effects(vis_car_count, alpha, true, 1.0f / 60.0f, false);
		if (render_profile_enabled) {
			const uint64_t now = render_profile_now_us();
			render_profile_effects_us += now - profile_step;
			profile_step = now;
		}
		apply_render_multimeshes(alpha);
		if (render_profile_enabled) {
			const uint64_t now = render_profile_now_us();
			render_profile_multimesh_us += now - profile_step;
			render_profile_body_instances += static_cast<uint64_t>(std::max(render_last_body_instances, 0));
			render_profile_thruster_instances += static_cast<uint64_t>(std::max(render_last_thruster_instances, 0));
			profile_step = now;
		}
		update_native_gameplay_camera(true);
		if (render_profile_enabled) {
			const uint64_t now = render_profile_now_us();
			render_profile_camera_us += now - profile_step;
			profile_step = now;
		}
		godot::Array local_visual_args;
		local_visual_args.resize(52);
		for (int i = 0; i < vis_cars.size(); i++) {
			godot::Object *vis_car = Object::cast_to<godot::Object>(vis_cars[i]);
			if (vis_car && static_cast<bool>(vis_car->get("local_visual_enabled"))) {
				const int32_t owner_id = static_cast<int32_t>(static_cast<int64_t>(vis_car->get("owning_id")));
				int car_index = -1;
				for (int n = 0; n < num_cars; ++n) {
					if (car_player_ids && car_player_ids[n] == owner_id) {
						car_index = n;
						break;
					}
				}
				if (car_index < 0 || car_index >= num_cars) {
					continue;
				}
				populate_visual_car_args(local_visual_args, cars[car_index]);
				vis_car->callv("apply_sim_state", local_visual_args);
			}
		}
		if (render_profile_enabled) {
			const uint64_t now = render_profile_now_us();
			render_profile_local_visual_us += now - profile_step;
			profile_step = now;
		}
		if (car_player_ids && car_is_cpu) {
			update_native_cpu_drivers();
		}
		if (render_profile_enabled) {
			const uint64_t now = render_profile_now_us();
			render_profile_cpu_driver_us += now - profile_step;
			profile_step = now;
		}
		update_super_spark_visuals();
		if (render_profile_enabled) {
			const uint64_t now = render_profile_now_us();
			render_profile_spark_us += now - profile_step;
			render_profile_total_us += now - profile_start;
			render_profile_frames += 1;
		}
		if (DEBUG::dip_enabled(DIP_SWITCH::DIP_DRAW_CHECKPOINTS))
		{
			for (int i = 0; i < current_track->num_checkpoints; i++)
			{
				current_track->checkpoints[i].debug_draw();
			}
		}
		if (DEBUG::dip_enabled(DIP_SWITCH::DIP_DRAW_BRANCH_CENTERLINE))
		{
			godot::Object* dd3d = godot::Engine::get_singleton()->get_singleton("DebugDraw3D");
			if (dd3d && num_cars > 0)
			{
				int cp_idx = cars[0].soa->current_checkpoint[cars[0].soa_index];
				if (cp_idx >= 0 && cp_idx < current_track->num_checkpoints)
				{
					std::vector<int> branch_indices;
					current_track->collect_branch_sequence(cp_idx, branch_indices);
					if (!branch_indices.empty())
					{
						for (size_t b = 0; b < branch_indices.size(); ++b)
						{
							int idx = branch_indices[b];
							if (idx < 0 || idx >= current_track->num_checkpoints)
							{
								continue;
							}
							const CollisionCheckpoint &cp = current_track->checkpoints[idx];
							dd3d->call("draw_line", gd_vec3(cp.position_start), gd_vec3(cp.position_end), godot::Color(1.0f, 0.9f, 0.1f), _TICK_DELTA);
							if (b + 1 < branch_indices.size())
							{
								int next_idx = branch_indices[b + 1];
								if (next_idx >= 0 && next_idx < current_track->num_checkpoints)
								{
									const CollisionCheckpoint &next_cp = current_track->checkpoints[next_idx];
									dd3d->call("draw_line", gd_vec3(cp.position_end), gd_vec3(next_cp.position_start), godot::Color(0.6f, 0.8f, 0.2f), _TICK_DELTA);
								}
							}
						}
					}
				}
			}
		}
		if (DEBUG::dip_enabled(DIP_SWITCH::DIP_DRAW_SEG_BOUNDS))
		{
			godot::Object* dd3d = godot::Engine::get_singleton()->get_singleton("DebugDraw3D");
			for (int i = 0; i < current_track->num_segments; i++)
			{
				dd3d->call("draw_aabb", gd_aabb(current_track->segments[i].bounds), godot::Color(1.0f, 0.0f, 1.0f, 0.1f), _TICK_DELTA);
			}
		}
		if (DEBUG::dip_enabled(DIP_SWITCH::DIP_DRAW_SEGMENT_SURF))
		{
		//DEBUG::disp_text("current checkpoint", cars[0].soa->current_checkpoint[cars[0].soa_index]);
			int use_seg_ind = current_track->checkpoints[cars[0].soa->current_checkpoint[cars[0].soa_index]].road_segment;
			for (int i = 0; i < current_track->num_segments; i++)
			{
				if (i > use_seg_ind + 1 || i < use_seg_ind - 1){
					continue;
				}
				godot::Object* dd3d = godot::Engine::get_singleton()->get_singleton("DebugDraw3D");

			const int x_subdiv = 16; // Adjust as needed
			const int y_subdiv = 32;  // Adjust as needed

			for (int yi = 0; yi <= y_subdiv; yi++)
			{
				float y_frac = static_cast<float>(yi) / y_subdiv;
				float y_val = y_frac; // Y: 0.0 to 1.0

				for (int xi = 0; xi <= x_subdiv; xi++)
				{
					float x_frac = static_cast<float>(xi) / x_subdiv;
					float x_val = -1.0f + 2.0f * x_frac; // X: -1.0 to +1.0

					// Interpolated color: red to blue across X, green from 0 to 1 across Y
					float r = 1.0f - x_frac;
					float g = y_frac;
					float b = x_frac;

					SimVec2 shape_pos(x_val, y_val);
					SimTransform road_transform;
					current_track->segments[i].road_shape->get_oriented_transform_at_time(road_transform, shape_pos);

					SimVec3 start = road_transform.origin;
					SimVec3 end = start + road_transform.basis[1] * 2.0f; // arrow in local Y/up

					dd3d->call("draw_arrow", gd_vec3(start), gd_vec3(end), godot::Color(r, g, b), 0.5, true, _TICK_DELTA);
				}
			}
		}
	}
}

void GameSim::save_state()
{
	int index = tick % STATE_BUFFER_LEN;
	int size = gamestate_data.get_size();
	state_buffer[index].size = size;
	state_buffer[index].tick = tick;
	save_bumper_states_to_saved_state(state_buffer[index]);
	update_saved_voice_transforms(state_buffer[index]);
	if (state_buffer[index].data)
	{
		memcpy(state_buffer[index].data, gamestate_data.heap_start, size);
	}
}

void GameSim::load_state(int target_tick)
{
	int index = target_tick % STATE_BUFFER_LEN;
	if (!state_buffer[index].data)
		return;
	const int correction_count = std::min(num_cars, static_cast<int>(render_rollback_corrections.size()));
	render_rollback_capture_transforms.clear();
	render_rollback_capture_pending = false;
	if (correction_count > 0 && cars) {
		render_rollback_capture_transforms.resize(correction_count);
		for (int i = 0; i < correction_count; ++i) {
			SimTransform current_visual = MXT_LOAD_TRANSFORM(*cars[i].soa, transform_visual, cars[i].soa_index);
			if (i < static_cast<int>(render_vehicle_visual_state.size())) {
				GameSim::RenderVehicleVisualState capture_visual_state = render_vehicle_visual_state[i];
				current_visual = compose_machine_visual_transform_for_render(
					*cars[i].soa,
					cars[i].soa_index,
					capture_visual_state,
					true,
					false);
			}
			const SimTransform predicted_transform = corrected_render_transform(
				render_rollback_corrections,
				render_rollback_correction_active,
				i,
				current_visual);
			render_rollback_capture_transforms[i] = predicted_transform;
		}
		render_rollback_capture_pending = true;
	}
	int size = state_buffer[index].size;
	memcpy(gamestate_data.heap_start, state_buffer[index].data, size);
	gamestate_data.set_size(size);
	tick = target_tick + 1;
	fix_pointers();
	restore_bumper_states_from_saved_state(state_buffer[index]);
}

void GameSim::finish_render_rollback_correction_capture()
{
	if (!render_rollback_capture_pending) {
		return;
	}
	render_rollback_capture_pending = false;
	const int correction_count = std::min(num_cars, static_cast<int>(render_rollback_capture_transforms.size()));
	if (correction_count > 0 && cars) {
		if (static_cast<int>(render_rollback_corrections.size()) < correction_count) {
			render_rollback_corrections.resize(correction_count);
			render_rollback_correction_active.resize(correction_count);
		}
		for (int i = 0; i < correction_count; ++i) {
			SimTransform current_visual = MXT_LOAD_TRANSFORM(*cars[i].soa, transform_visual, cars[i].soa_index);
			if (i < static_cast<int>(render_vehicle_visual_state.size())) {
				GameSim::RenderVehicleVisualState capture_visual_state = render_vehicle_visual_state[i];
				current_visual = compose_machine_visual_transform_for_render(
					*cars[i].soa,
					cars[i].soa_index,
					capture_visual_state,
					true,
					false);
			}
			SimTransform correction;
			correction.origin = render_rollback_capture_transforms[i].origin - current_visual.origin;
			correction.basis = current_visual.basis.transposed() * render_rollback_capture_transforms[i].basis;
			render_rollback_corrections[i] = correction;
			render_rollback_correction_active[i] = render_correction_is_small(correction) ? 0 : 1;
		}
	}
	render_rollback_capture_transforms.clear();
}

namespace {
constexpr uint32_t MXT_NET_STATE_MAGIC = 0x5354584du; // "MXTS", little-endian.

static uint16_t mxt_float_to_half_bits(float value) {
	uint32_t bits = 0;
	std::memcpy(&bits, &value, sizeof(bits));
	const uint32_t sign = (bits >> 16) & 0x8000u;
	int32_t exp = static_cast<int32_t>((bits >> 23) & 0xffu) - 127 + 15;
	uint32_t mant = bits & 0x7fffffu;
	if (exp <= 0) {
		if (exp < -10) {
			return static_cast<uint16_t>(sign);
		}
		mant |= 0x800000u;
		const uint32_t shift = static_cast<uint32_t>(14 - exp);
		uint32_t half_mant = mant >> shift;
		if ((mant >> (shift - 1)) & 1u) {
			half_mant += 1u;
		}
		return static_cast<uint16_t>(sign | half_mant);
	}
	if (exp >= 31) {
		return static_cast<uint16_t>(sign | 0x7c00u);
	}
	uint32_t half = sign | (static_cast<uint32_t>(exp) << 10) | (mant >> 13);
	if (mant & 0x1000u) {
		half += 1u;
	}
	return static_cast<uint16_t>(half);
}

static float mxt_half_bits_to_float(uint16_t half) {
	const uint32_t sign = (static_cast<uint32_t>(half & 0x8000u)) << 16;
	uint32_t exp = (half >> 10) & 0x1fu;
	uint32_t mant = half & 0x03ffu;
	uint32_t bits = 0;
	if (exp == 0) {
		if (mant == 0) {
			bits = sign;
		} else {
			exp = 1;
			while ((mant & 0x0400u) == 0) {
				mant <<= 1;
				--exp;
			}
			mant &= 0x03ffu;
			bits = sign | ((exp + (127 - 15)) << 23) | (mant << 13);
		}
	} else if (exp == 31) {
		bits = sign | 0x7f800000u | (mant << 13);
	} else {
		bits = sign | ((exp + (127 - 15)) << 23) | (mant << 13);
	}
	float value = 0.0f;
	std::memcpy(&value, &bits, sizeof(value));
	return value;
}

static bool mxt_net_checkpoint_surface_at(RaceTrack* track, int cp_idx, float fraction, SimTransform& out) {
	if (!track || cp_idx < 0 || cp_idx >= track->num_checkpoints) {
		return false;
	}
	const CollisionCheckpoint& cp = track->checkpoints[cp_idx];
	if (cp.road_segment < 0 || cp.road_segment >= track->num_segments || !track->segments[cp.road_segment].road_shape) {
		return false;
	}
	const float t_y = cp.t_start + (cp.t_end - cp.t_start) * std::clamp(fraction, 0.0f, 1.0f);
	track->segments[cp.road_segment].road_shape->get_oriented_transform_at_time(out, SimVec2(0.0f, t_y));
	out.basis.orthonormalize();
	return true;
}

static SimTransform mxt_net_checkpoint_surface_or_identity(RaceTrack* track, int cp_idx, float fraction) {
	SimTransform surface;
	if (!mxt_net_checkpoint_surface_at(track, cp_idx, fraction, surface)) {
		surface = SimTransform();
	}
	return surface;
}

struct NetStateWriter {
	std::vector<uint8_t> data;

	template <typename T>
	void write_pod(const T& value) {
		const uint8_t* src = reinterpret_cast<const uint8_t*>(&value);
		data.insert(data.end(), src, src + sizeof(T));
	}

	void write_bytes(const void* src, size_t size) {
		if (!src || size == 0) {
			return;
		}
		const uint8_t* bytes = reinterpret_cast<const uint8_t*>(src);
		data.insert(data.end(), bytes, bytes + size);
	}

	void write_vec3(const SimVec3& v) {
		write_pod(v.x);
		write_pod(v.y);
		write_pod(v.z);
	}

	void write_vec3_half(const SimVec3& v) {
		write_float16(v.x);
		write_float16(v.y);
		write_float16(v.z);
	}

	void write_vec2(const SimVec2& v) {
		write_pod(v.x);
		write_pod(v.y);
	}

	void write_quat(const SimQuat& q) {
		write_pod(q.x);
		write_pod(q.y);
		write_pod(q.z);
		write_pod(q.w);
	}

	void write_quat_i16(const SimQuat& q) {
		const float len_sq = q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w;
		const float inv_len = len_sq > 0.000001f ? 1.0f / std::sqrt(len_sq) : 1.0f;
		const auto pack = [&](float v) -> int16_t {
			const float clamped = std::clamp(v * inv_len, -1.0f, 1.0f);
			return static_cast<int16_t>(std::lround(clamped * 32767.0f));
		};
		write_pod(pack(q.x));
		write_pod(pack(q.y));
		write_pod(pack(q.z));
		write_pod(pack(q.w));
	}

	void write_float16(float value) {
		write_pod(mxt_float_to_half_bits(value));
	}

	void write_basis(const SimBasis& b) {
		for (int col = 0; col < 3; ++col) {
			write_vec3(b.get_column(col));
		}
	}

	void write_transform(const SimTransform& t) {
		for (int col = 0; col < 3; ++col) {
			write_vec3(t.basis.get_column(col));
		}
		write_vec3(t.origin);
	}

	int size() const {
		return static_cast<int>(data.size());
	}

	godot::PackedByteArray to_packed_byte_array() const {
		godot::PackedByteArray out;
		out.resize(static_cast<int>(data.size()));
		if (!data.empty()) {
			std::memcpy(out.ptrw(), data.data(), data.size());
		}
		return out;
	}
};

struct NetStateReader {
	const uint8_t* data = nullptr;
	int size = 0;
	int pos = 0;

	explicit NetStateReader(const godot::PackedByteArray& bytes) {
		data = bytes.ptr();
		size = bytes.size();
	}

	template <typename T>
	bool read_pod(T& out) {
		if (pos < 0 || pos + static_cast<int>(sizeof(T)) > size) {
			return false;
		}
		std::memcpy(&out, data + pos, sizeof(T));
		pos += static_cast<int>(sizeof(T));
		return true;
	}

	bool read_bytes(void* dst, size_t byte_count) {
		if (byte_count == 0) {
			return true;
		}
		if (!dst || pos < 0 || pos + static_cast<int>(byte_count) > size) {
			return false;
		}
		std::memcpy(dst, data + pos, byte_count);
		pos += static_cast<int>(byte_count);
		return true;
	}

	bool read_vec3(SimVec3& out) {
		return read_pod(out.x) && read_pod(out.y) && read_pod(out.z);
	}

	bool read_vec3_half(SimVec3& out) {
		return read_float16(out.x) && read_float16(out.y) && read_float16(out.z);
	}

	bool read_vec2(SimVec2& out) {
		return read_pod(out.x) && read_pod(out.y);
	}

	bool read_quat(SimQuat& out) {
		return read_pod(out.x) && read_pod(out.y) && read_pod(out.z) && read_pod(out.w);
	}

	bool read_quat_i16(SimQuat& out) {
		int16_t x = 0;
		int16_t y = 0;
		int16_t z = 0;
		int16_t w = 0;
		if (!read_pod(x) || !read_pod(y) || !read_pod(z) || !read_pod(w)) {
			return false;
		}
		out.x = static_cast<float>(x) / 32767.0f;
		out.y = static_cast<float>(y) / 32767.0f;
		out.z = static_cast<float>(z) / 32767.0f;
		out.w = static_cast<float>(w) / 32767.0f;
		const float len_sq = out.x * out.x + out.y * out.y + out.z * out.z + out.w * out.w;
		if (len_sq > 0.000001f) {
			const float inv_len = 1.0f / std::sqrt(len_sq);
			out.x *= inv_len;
			out.y *= inv_len;
			out.z *= inv_len;
			out.w *= inv_len;
		} else {
			out = SimQuat();
		}
		return true;
	}

	bool read_float16(float& out) {
		uint16_t bits = 0;
		if (!read_pod(bits)) {
			return false;
		}
		out = mxt_half_bits_to_float(bits);
		return true;
	}

	bool read_basis(SimBasis& out) {
		SimVec3 c0, c1, c2;
		if (!read_vec3(c0) || !read_vec3(c1) || !read_vec3(c2)) {
			return false;
		}
		out.set_column(0, c0);
		out.set_column(1, c1);
		out.set_column(2, c2);
		return true;
	}

	bool read_transform(SimTransform& out) {
		SimVec3 c0, c1, c2;
		if (!read_vec3(c0) || !read_vec3(c1) || !read_vec3(c2) || !read_vec3(out.origin)) {
			return false;
		}
		out.basis.set_column(0, c0);
		out.basis.set_column(1, c1);
		out.basis.set_column(2, c2);
		return true;
	}
};
}

#define MXT_NET_CAR_SCALAR_FIELDS(X) \
	X(uint32_t, machine_state) \
	X(uint16_t, boost_frames) \
	X(uint16_t, boost_frames_manual) \
	X(uint32_t, last_hit_tick) \
	X(uint8_t, spinattack_direction) \
	X(uint8_t, brake_timer) \
	X(uint32_t, terrain_state) \
	X(uint32_t, frames_since_start) \
	X(uint8_t, frames_since_start_2) \
	X(uint8_t, air_time) \
	X(uint32_t, strafe_effect) \
	X(uint8_t, frames_since_death) \
	X(uint32_t, state_2) \
	X(uint16_t, g_anim_timer) \
	X(uint32_t, level_start_time) \
	X(uint16_t, some_breakdown_int) \
	X(uint8_t, breakdown_frame_counter) \
	X(uint16_t, restore_wait_frames) \
	X(uint16_t, restore_move_frames) \
	X(uint16_t, current_checkpoint) \
	X(int16_t, current_collision_checkpoint) \
	X(uint16_t, last_ground_checkpoint) \
	X(uint8_t, lap) \
	X(uint8_t, broken_lap_rollback_lap) \
	X(uint8_t, rail_collision_timer) \
	X(uint8_t, grip_frames_from_accel_press) \
	X(uint8_t, side_attack_delay) \
	X(uint16_t, attack_cooldown_frames) \
	X(uint8_t, machine_collision_frame_counter) \
	X(uint8_t, car_hit_invincibility) \
	X(uint8_t, boost_delay_frame_counter) \
	X(int8_t, drift_sign) \
	X(uint8_t, restore_state) \
	X(uint16_t, s_boost_charge) \
	X(uint16_t, s_boost_charge_max) \
	X(uint16_t, s_boost_frames_remaining) \
	X(uint8_t, s_boost_emit_frame_accumulator) \
	X(uint8_t, s_boost_pending_spark_spawns) \
	X(uint8_t, pending_super_sparks) \
	X(bool, has_last_hit_tick) \
	X(bool, machine_crashed) \
	X(bool, s_boost_active) \
	X(bool, broken_lap_rollback_pending) \
	X(float, base_speed) \
	X(float, boost_turbo) \
	X(float, dashplate_heat_multiplier) \
	X(float, race_start_charge) \
	X(float, air_tilt) \
	X(float, energy) \
	X(float, ko_energy_bonus) \
	X(float, spinattack_angle) \
	X(float, spinattack_decrement) \
	X(float, height_above_track) \
	X(float, last_ground_distance) \
	X(float, previous_lap_distance) \
	X(float, checkpoint_fraction) \
	X(float, input_strafe_32) \
	X(float, input_strafe_1_6) \
	X(float, input_accel) \
	X(float, damage_from_last_hit) \
	X(float, turn_reaction_input) \
	X(float, turning_related) \
	X(float, drift_ramp) \
	X(float, side_attack_indicator)

#define MXT_NET_CAR_VEC3_FIELDS(X) \
	X(velocity)

#define MXT_NET_CAR_VEC3_HALF_FIELDS(X) \
	X(track_surface_normal) \
	X(knockback_velocity) \
	X(velocity_angular)

#define MXT_NET_CAR_TRANSFORM_FIELDS(X)

#define MXT_NET_TILT_SCALAR_FIELDS(X) \
	X(float, force) \
	X(uint8_t, state)

#define MXT_NET_TILT_VEC3_FIELDS(X)

#define MXT_NET_WALL_VEC3_FIELDS(X)

godot::PackedByteArray GameSim::serialize_network_state(int target_tick) const {
	NetStateWriter writer;
	NetworkStateSizeStats stats;
	stats.car_count = num_cars;
	stats.bumper_count = bumper_count;
	int section_start = writer.size();
	writer.write_pod(MXT_NET_STATE_MAGIC);
	writer.write_pod(static_cast<uint16_t>(0));
	writer.write_pod(static_cast<int32_t>(target_tick));
	writer.write_pod(static_cast<int32_t>(num_cars));
	writer.write_pod(static_cast<int32_t>(bumper_count));
	writer.write_pod(bumper_track_seed);
	writer.write_pod(bumper_scheduler_lap);
	writer.write_pod(bumper_next_sequence);
	stats.header += writer.size() - section_start;
	section_start = writer.size();
	for (int i = 0; i < bumper_count && i < BUMPER_POOL_SIZE; ++i) {
		const BumperState& state = bumper_states[i];
		if (state.active) {
			++stats.active_bumper_count;
		}
		writer.write_pod(state.active);
		writer.write_pod(state.spawn_lap);
		writer.write_pod(state.next_sequence);
		writer.write_pod(state.target_lane);
	}
	stats.bumper_meta += writer.size() - section_start;

	const int trigger_count = current_track ? current_track->num_trigger_colliders : 0;
	stats.trigger_count = trigger_count;
	section_start = writer.size();
	writer.write_pod(static_cast<int32_t>(trigger_count));
	uint16_t active_spark_count = 0;
	if (super_spark_state) {
		for (uint16_t i = 0; i < SUPER_SPARK_CAPACITY; ++i) {
			if (super_spark_state->sparks[i].active) {
				++active_spark_count;
			}
		}
		writer.write_pod(super_spark_state->cursor);
		writer.write_pod(super_spark_state->rng_state);
		writer.write_pod(super_spark_state->placement_timer);
		writer.write_pod(active_spark_count);
		for (uint16_t i = 0; i < SUPER_SPARK_CAPACITY; ++i) {
			const SuperSpark& spark = super_spark_state->sparks[i];
			if (!spark.active) {
				continue;
			}
			const uint8_t spark_flags = spark.collectable ? 1u : 0u;
			writer.write_pod(i);
			writer.write_pod(spark_flags);
			writer.write_pod(spark.checkpoint);
			writer.write_vec3_half(spark.final_position);
			if (!spark.collectable) {
				writer.write_pod(spark.animation_frame);
				writer.write_vec3_half(spark.start_position);
				writer.write_vec3_half(spark.plane_normal);
			}
		}
	} else {
		uint16_t cursor = 0;
		uint32_t rng_state = 0;
		uint32_t placement_timer = 0;
		writer.write_pod(cursor);
		writer.write_pod(rng_state);
		writer.write_pod(placement_timer);
		writer.write_pod(active_spark_count);
	}
	stats.active_spark_count = static_cast<int>(active_spark_count);
	stats.sparks += writer.size() - section_start;

	section_start = writer.size();
#define WRITE_NET_SCALAR(type, name) \
	for (int i = 0; i < num_cars; ++i) { \
		PhysicsCarSoA& soa = *cars[i].soa; \
		const int lane = cars[i].soa_index; \
		if constexpr (std::is_same_v<type, float>) { \
			writer.write_float16(soa.name[lane]); \
		} else { \
			const type wire_value = static_cast<type>(soa.name[lane]); \
			writer.write_pod(wire_value); \
		} \
	}
	MXT_NET_CAR_SCALAR_FIELDS(WRITE_NET_SCALAR)
#undef WRITE_NET_SCALAR
	stats.car_scalars += writer.size() - section_start;

	section_start = writer.size();
#define WRITE_NET_BUMPER_SCALAR(type, name) \
	for (int i = 0; i < bumper_count; ++i) { \
		PhysicsCarSoA& soa = *bumper_cars[i].soa; \
		const int lane = bumper_cars[i].soa_index; \
		if constexpr (std::is_same_v<type, float>) { \
			writer.write_float16(soa.name[lane]); \
		} else { \
			const type wire_value = static_cast<type>(soa.name[lane]); \
			writer.write_pod(wire_value); \
		} \
	}
	if (bumper_cars) { \
		MXT_NET_CAR_SCALAR_FIELDS(WRITE_NET_BUMPER_SCALAR) \
	}
#undef WRITE_NET_BUMPER_SCALAR
	stats.bumper_scalars += writer.size() - section_start;

	section_start = writer.size();
#define WRITE_NET_VEC3_COMPONENT(name, component) \
	for (int i = 0; i < num_cars; ++i) { \
		PhysicsCarSoA& soa = *cars[i].soa; \
		const int lane = cars[i].soa_index; \
		writer.write_pod(soa.name##_##component[lane]); \
	}
#define WRITE_NET_VEC3(name) \
	WRITE_NET_VEC3_COMPONENT(name, x) \
	WRITE_NET_VEC3_COMPONENT(name, y) \
	WRITE_NET_VEC3_COMPONENT(name, z)
	MXT_NET_CAR_VEC3_FIELDS(WRITE_NET_VEC3)
#undef WRITE_NET_VEC3
#undef WRITE_NET_VEC3_COMPONENT
	for (int i = 0; i < num_cars; ++i) {
		PhysicsCarSoA& soa = *cars[i].soa;
		const int lane = cars[i].soa_index;
		const SimTransform surface = mxt_net_checkpoint_surface_or_identity(current_track, soa.current_checkpoint[lane], soa.checkpoint_fraction[lane]);
		writer.write_vec3_half(surface.xform_inv(LOAD_INDEXED_VEC3(soa, position_current, lane)));
		writer.write_vec3_half(surface.xform_inv(LOAD_INDEXED_VEC3(soa, position_old, lane)));
	}
	for (int i = 0; i < num_cars; ++i) {
		PhysicsCarSoA& soa = *cars[i].soa;
		const int lane = cars[i].soa_index;
#define WRITE_NET_VEC3_HALF(name) writer.write_vec3_half(LOAD_INDEXED_VEC3(soa, name, lane));
		MXT_NET_CAR_VEC3_HALF_FIELDS(WRITE_NET_VEC3_HALF)
#undef WRITE_NET_VEC3_HALF
	}
	stats.car_vec3 += writer.size() - section_start;

	section_start = writer.size();
#define WRITE_NET_BUMPER_VEC3_COMPONENT(name, component) \
	for (int i = 0; i < bumper_count; ++i) { \
		PhysicsCarSoA& soa = *bumper_cars[i].soa; \
		const int lane = bumper_cars[i].soa_index; \
		writer.write_pod(soa.name##_##component[lane]); \
	}
#define WRITE_NET_BUMPER_VEC3(name) \
	WRITE_NET_BUMPER_VEC3_COMPONENT(name, x) \
	WRITE_NET_BUMPER_VEC3_COMPONENT(name, y) \
	WRITE_NET_BUMPER_VEC3_COMPONENT(name, z)
	if (bumper_cars) { \
		MXT_NET_CAR_VEC3_FIELDS(WRITE_NET_BUMPER_VEC3) \
	}
#undef WRITE_NET_BUMPER_VEC3
#undef WRITE_NET_BUMPER_VEC3_COMPONENT
	if (bumper_cars) {
		for (int i = 0; i < bumper_count; ++i) {
			PhysicsCarSoA& soa = *bumper_cars[i].soa;
			const int lane = bumper_cars[i].soa_index;
			const SimTransform surface = mxt_net_checkpoint_surface_or_identity(current_track, soa.current_checkpoint[lane], soa.checkpoint_fraction[lane]);
			writer.write_vec3_half(surface.xform_inv(LOAD_INDEXED_VEC3(soa, position_current, lane)));
			writer.write_vec3_half(surface.xform_inv(LOAD_INDEXED_VEC3(soa, position_old, lane)));
		}
	}
	if (bumper_cars) {
		for (int i = 0; i < bumper_count; ++i) {
			PhysicsCarSoA& soa = *bumper_cars[i].soa;
			const int lane = bumper_cars[i].soa_index;
#define WRITE_NET_BUMPER_VEC3_HALF(name) writer.write_vec3_half(LOAD_INDEXED_VEC3(soa, name, lane));
			MXT_NET_CAR_VEC3_HALF_FIELDS(WRITE_NET_BUMPER_VEC3_HALF)
#undef WRITE_NET_BUMPER_VEC3_HALF
		}
	}
	stats.bumper_vec3 += writer.size() - section_start;

	section_start = writer.size();
#define WRITE_NET_TRANSFORM_COMPONENT(name, component) \
	for (int i = 0; i < num_cars; ++i) { \
		PhysicsCarSoA& soa = *cars[i].soa; \
		const int lane = cars[i].soa_index; \
		writer.write_pod(soa.name##_##component[lane]); \
	}
#define WRITE_NET_TRANSFORM(name) \
	WRITE_NET_TRANSFORM_COMPONENT(name, c0x) \
	WRITE_NET_TRANSFORM_COMPONENT(name, c0y) \
	WRITE_NET_TRANSFORM_COMPONENT(name, c0z) \
	WRITE_NET_TRANSFORM_COMPONENT(name, c1x) \
	WRITE_NET_TRANSFORM_COMPONENT(name, c1y) \
	WRITE_NET_TRANSFORM_COMPONENT(name, c1z) \
	WRITE_NET_TRANSFORM_COMPONENT(name, c2x) \
	WRITE_NET_TRANSFORM_COMPONENT(name, c2y) \
	WRITE_NET_TRANSFORM_COMPONENT(name, c2z) \
	WRITE_NET_TRANSFORM_COMPONENT(name, ox) \
	WRITE_NET_TRANSFORM_COMPONENT(name, oy) \
	WRITE_NET_TRANSFORM_COMPONENT(name, oz)
	MXT_NET_CAR_TRANSFORM_FIELDS(WRITE_NET_TRANSFORM)
#undef WRITE_NET_TRANSFORM
#undef WRITE_NET_TRANSFORM_COMPONENT
	stats.car_transform += writer.size() - section_start;

	section_start = writer.size();
#define WRITE_NET_BUMPER_TRANSFORM_COMPONENT(name, component) \
	for (int i = 0; i < bumper_count; ++i) { \
		PhysicsCarSoA& soa = *bumper_cars[i].soa; \
		const int lane = bumper_cars[i].soa_index; \
		writer.write_pod(soa.name##_##component[lane]); \
	}
#define WRITE_NET_BUMPER_TRANSFORM(name) \
	WRITE_NET_BUMPER_TRANSFORM_COMPONENT(name, c0x) \
	WRITE_NET_BUMPER_TRANSFORM_COMPONENT(name, c0y) \
	WRITE_NET_BUMPER_TRANSFORM_COMPONENT(name, c0z) \
	WRITE_NET_BUMPER_TRANSFORM_COMPONENT(name, c1x) \
	WRITE_NET_BUMPER_TRANSFORM_COMPONENT(name, c1y) \
	WRITE_NET_BUMPER_TRANSFORM_COMPONENT(name, c1z) \
	WRITE_NET_BUMPER_TRANSFORM_COMPONENT(name, c2x) \
	WRITE_NET_BUMPER_TRANSFORM_COMPONENT(name, c2y) \
	WRITE_NET_BUMPER_TRANSFORM_COMPONENT(name, c2z) \
	WRITE_NET_BUMPER_TRANSFORM_COMPONENT(name, ox) \
	WRITE_NET_BUMPER_TRANSFORM_COMPONENT(name, oy) \
	WRITE_NET_BUMPER_TRANSFORM_COMPONENT(name, oz)
	if (bumper_cars) { \
		MXT_NET_CAR_TRANSFORM_FIELDS(WRITE_NET_BUMPER_TRANSFORM) \
	}
#undef WRITE_NET_BUMPER_TRANSFORM
#undef WRITE_NET_BUMPER_TRANSFORM_COMPONENT
	stats.bumper_transform += writer.size() - section_start;

	section_start = writer.size();
#define WRITE_NET_BASIS(name) \
	for (int i = 0; i < num_cars; ++i) { \
		PhysicsCarSoA& soa = *cars[i].soa; \
		const int lane = cars[i].soa_index; \
		writer.write_quat_i16(MXT_LOAD_TRANSFORM(soa, name, lane).basis.get_rotation_quaternion()); \
	}
	WRITE_NET_BASIS(basis_physical)
	WRITE_NET_BASIS(basis_physical_other)
#undef WRITE_NET_BASIS
	stats.car_basis += writer.size() - section_start;

	section_start = writer.size();
#define WRITE_NET_BUMPER_BASIS(name) \
	for (int i = 0; i < bumper_count; ++i) { \
		PhysicsCarSoA& soa = *bumper_cars[i].soa; \
		const int lane = bumper_cars[i].soa_index; \
		writer.write_quat_i16(MXT_LOAD_TRANSFORM(soa, name, lane).basis.get_rotation_quaternion()); \
	}
	if (bumper_cars) { \
		WRITE_NET_BUMPER_BASIS(basis_physical) \
		WRITE_NET_BUMPER_BASIS(basis_physical_other) \
	}
#undef WRITE_NET_BUMPER_BASIS
	stats.bumper_basis += writer.size() - section_start;

	section_start = writer.size();
	for (int i = 0; i < num_cars; ++i) {
		PhysicsCarSoA& soa = *cars[i].soa;
		const int lane = cars[i].soa_index;
		if (soa.collision_old_valid[lane]) {
			++stats.car_collision_old_count;
		}
		if (soa.restore_state[lane] != 0) {
			++stats.car_restore_count;
			writer.write_transform(MXT_LOAD_TRANSFORM(soa, restore_start_transform, lane));
			writer.write_transform(MXT_LOAD_TRANSFORM(soa, restore_target_transform, lane));
		}
	}
	stats.car_conditionals += writer.size() - section_start;
	section_start = writer.size();
	if (bumper_cars) {
		for (int i = 0; i < bumper_count; ++i) {
			PhysicsCarSoA& soa = *bumper_cars[i].soa;
			const int lane = bumper_cars[i].soa_index;
			if (soa.collision_old_valid[lane]) {
				++stats.bumper_collision_old_count;
			}
			if (soa.restore_state[lane] != 0) {
				++stats.bumper_restore_count;
				writer.write_transform(MXT_LOAD_TRANSFORM(soa, restore_start_transform, lane));
				writer.write_transform(MXT_LOAD_TRANSFORM(soa, restore_target_transform, lane));
			}
		}
	}
	stats.bumper_conditionals += writer.size() - section_start;

	section_start = writer.size();
#define WRITE_NET_TILT_SCALAR(type, name) \
	for (int point = 0; point < 4; ++point) { \
		for (int i = 0; i < num_cars; ++i) { \
			PhysicsCarSoA& soa = *cars[i].soa; \
			const int p = cars[i].soa_index * 4 + point; \
			if constexpr (std::is_same_v<type, float>) { \
				writer.write_float16(soa.tilt_##name[p]); \
			} else { \
				const type wire_value = static_cast<type>(soa.tilt_##name[p]); \
				writer.write_pod(wire_value); \
			} \
		} \
	}
	MXT_NET_TILT_SCALAR_FIELDS(WRITE_NET_TILT_SCALAR)
#undef WRITE_NET_TILT_SCALAR
	stats.car_tilt += writer.size() - section_start;

	section_start = writer.size();
#define WRITE_NET_BUMPER_TILT_SCALAR(type, name) \
	for (int point = 0; point < 4; ++point) { \
		for (int i = 0; i < bumper_count; ++i) { \
			PhysicsCarSoA& soa = *bumper_cars[i].soa; \
			const int p = bumper_cars[i].soa_index * 4 + point; \
			if constexpr (std::is_same_v<type, float>) { \
				writer.write_float16(soa.tilt_##name[p]); \
			} else { \
				const type wire_value = static_cast<type>(soa.tilt_##name[p]); \
				writer.write_pod(wire_value); \
			} \
		} \
	}
	if (bumper_cars) { \
		MXT_NET_TILT_SCALAR_FIELDS(WRITE_NET_BUMPER_TILT_SCALAR) \
	}
#undef WRITE_NET_BUMPER_TILT_SCALAR
	stats.bumper_tilt += writer.size() - section_start;

	section_start = writer.size();
#define WRITE_NET_TILT_VEC3_COMPONENT(name, component) \
	for (int point = 0; point < 4; ++point) { \
		for (int i = 0; i < num_cars; ++i) { \
			PhysicsCarSoA& soa = *cars[i].soa; \
			const int p = cars[i].soa_index * 4 + point; \
			writer.write_pod(soa.tilt_##name##_##component[p]); \
		} \
	}
#define WRITE_NET_TILT_VEC3(name) \
	WRITE_NET_TILT_VEC3_COMPONENT(name, x) \
	WRITE_NET_TILT_VEC3_COMPONENT(name, y) \
	WRITE_NET_TILT_VEC3_COMPONENT(name, z)
	MXT_NET_TILT_VEC3_FIELDS(WRITE_NET_TILT_VEC3)
#undef WRITE_NET_TILT_VEC3
#undef WRITE_NET_TILT_VEC3_COMPONENT
	stats.car_tilt += writer.size() - section_start;

	section_start = writer.size();
#define WRITE_NET_BUMPER_TILT_VEC3_COMPONENT(name, component) \
	for (int point = 0; point < 4; ++point) { \
		for (int i = 0; i < bumper_count; ++i) { \
			PhysicsCarSoA& soa = *bumper_cars[i].soa; \
			const int p = bumper_cars[i].soa_index * 4 + point; \
			writer.write_pod(soa.tilt_##name##_##component[p]); \
		} \
	}
#define WRITE_NET_BUMPER_TILT_VEC3(name) \
	WRITE_NET_BUMPER_TILT_VEC3_COMPONENT(name, x) \
	WRITE_NET_BUMPER_TILT_VEC3_COMPONENT(name, y) \
	WRITE_NET_BUMPER_TILT_VEC3_COMPONENT(name, z)
	if (bumper_cars) { \
		MXT_NET_TILT_VEC3_FIELDS(WRITE_NET_BUMPER_TILT_VEC3) \
	}
#undef WRITE_NET_BUMPER_TILT_VEC3
#undef WRITE_NET_BUMPER_TILT_VEC3_COMPONENT
	stats.bumper_tilt += writer.size() - section_start;

	section_start = writer.size();
#define WRITE_NET_WALL_VEC3_COMPONENT(name, component) \
	for (int point = 0; point < 4; ++point) { \
		for (int i = 0; i < num_cars; ++i) { \
			PhysicsCarSoA& soa = *cars[i].soa; \
			const int p = cars[i].soa_index * 4 + point; \
			writer.write_pod(soa.wall_##name##_##component[p]); \
		} \
	}
#define WRITE_NET_WALL_VEC3(name) \
	WRITE_NET_WALL_VEC3_COMPONENT(name, x) \
	WRITE_NET_WALL_VEC3_COMPONENT(name, y) \
	WRITE_NET_WALL_VEC3_COMPONENT(name, z)
	MXT_NET_WALL_VEC3_FIELDS(WRITE_NET_WALL_VEC3)
#undef WRITE_NET_WALL_VEC3
#undef WRITE_NET_WALL_VEC3_COMPONENT
	stats.car_wall += writer.size() - section_start;

	section_start = writer.size();
#define WRITE_NET_BUMPER_WALL_VEC3_COMPONENT(name, component) \
	for (int point = 0; point < 4; ++point) { \
		for (int i = 0; i < bumper_count; ++i) { \
			PhysicsCarSoA& soa = *bumper_cars[i].soa; \
			const int p = bumper_cars[i].soa_index * 4 + point; \
			writer.write_pod(soa.wall_##name##_##component[p]); \
		} \
	}
#define WRITE_NET_BUMPER_WALL_VEC3(name) \
	WRITE_NET_BUMPER_WALL_VEC3_COMPONENT(name, x) \
	WRITE_NET_BUMPER_WALL_VEC3_COMPONENT(name, y) \
	WRITE_NET_BUMPER_WALL_VEC3_COMPONENT(name, z)
	if (bumper_cars) { \
		MXT_NET_WALL_VEC3_FIELDS(WRITE_NET_BUMPER_WALL_VEC3) \
	}
#undef WRITE_NET_BUMPER_WALL_VEC3
#undef WRITE_NET_BUMPER_WALL_VEC3_COMPONENT
	stats.bumper_wall += writer.size() - section_start;

	section_start = writer.size();
	for (int i = 0; i < trigger_count; ++i) {
		TriggerCollider* trigger = current_track->trigger_colliders[i];
		uint8_t exploded = 0;
		float heat = 0.0f;
		uint32_t last_activation_tick = 0;
		uint8_t has_last_activation = 0;
		if (trigger && trigger->type == TRIGGER_TYPE::MINE) {
			exploded = static_cast<Mine*>(trigger)->exploded ? 1 : 0;
		} else if (trigger && trigger->type == TRIGGER_TYPE::DASHPLATE) {
			Dashplate* dash = static_cast<Dashplate*>(trigger);
			heat = dash->heat;
			last_activation_tick = dash->last_activation_tick;
			has_last_activation = dash->has_last_activation ? 1 : 0;
		}
		writer.write_pod(exploded);
		writer.write_pod(heat);
		writer.write_pod(last_activation_tick);
		writer.write_pod(has_last_activation);
	}
	stats.triggers += writer.size() - section_start;

	stats.total = writer.size();
	last_network_state_size_stats = stats;
	return writer.to_packed_byte_array();
}

bool GameSim::deserialize_network_state(int target_tick, const godot::PackedByteArray& data) {
	NetStateReader reader(data);
	uint32_t magic = 0;
	uint16_t flags = 0;
	int32_t snapshot_tick = 0;
	int32_t snapshot_cars = 0;
	int32_t snapshot_bumper_count = 0;
	int32_t trigger_count = 0;
	if (!reader.read_pod(magic) || magic != MXT_NET_STATE_MAGIC ||
		!reader.read_pod(flags) ||
		!reader.read_pod(snapshot_tick) ||
		!reader.read_pod(snapshot_cars) ||
		!reader.read_pod(snapshot_bumper_count) ||
		!reader.read_pod(bumper_track_seed) ||
		!reader.read_pod(bumper_scheduler_lap) ||
		!reader.read_pod(bumper_next_sequence)) {
		return false;
	}
	(void)flags;
	(void)snapshot_tick;
	if (snapshot_cars != num_cars ||
			snapshot_bumper_count < 0 ||
			snapshot_bumper_count != bumper_count ||
			snapshot_bumper_count > BUMPER_POOL_SIZE) {
		return false;
	}
	for (int i = 0; i < snapshot_bumper_count; ++i) {
		BumperState& state = bumper_states[i];
		if (!reader.read_pod(state.active) ||
				!reader.read_pod(state.spawn_lap) ||
				!reader.read_pod(state.next_sequence) ||
				!reader.read_pod(state.target_lane)) {
			return false;
		}
		if (state.active > 1 || !std::isfinite(state.target_lane)) {
			return false;
		}
	}
	if (!reader.read_pod(trigger_count)) {
		return false;
	}
	if (trigger_count < 0) {
		return false;
	}
	if (!super_spark_state) {
		return false;
	}
	uint16_t spark_cursor = 0;
	uint32_t spark_rng_state = 0;
	uint32_t spark_placement_timer = 0;
	uint16_t active_spark_count = 0;
	if (!reader.read_pod(spark_cursor) ||
		!reader.read_pod(spark_rng_state) ||
		!reader.read_pod(spark_placement_timer) ||
		!reader.read_pod(active_spark_count)) {
		return false;
	}
	super_spark_state->cursor = spark_cursor;
	super_spark_state->rng_state = spark_rng_state;
	super_spark_state->placement_timer = spark_placement_timer;
	for (uint16_t i = 0; i < SUPER_SPARK_CAPACITY; ++i) {
		super_spark_state->sparks[i] = SuperSpark();
	}
	for (uint16_t n = 0; n < active_spark_count; ++n) {
		uint16_t spark_index = 0;
		if (!reader.read_pod(spark_index) || spark_index >= SUPER_SPARK_CAPACITY) {
			return false;
		}
		uint8_t spark_flags = 0;
		SuperSpark& spark = super_spark_state->sparks[spark_index];
		if (!reader.read_pod(spark_flags) ||
			!reader.read_pod(spark.checkpoint) ||
			!reader.read_vec3_half(spark.final_position)) {
			return false;
		}
		spark.active = 1;
		spark.collectable = (spark_flags & 1u) != 0u ? 1u : 0u;
		if (spark.collectable) {
			spark.animation_frame = MXT_SUPER_SPARK_ANIMATION_FRAMES;
			spark.start_position = spark.final_position;
			spark.plane_normal = SimVec3(0.0f, 1.0f, 0.0f);
			spark.position = spark.final_position;
			spark.prev_position = spark.final_position;
		} else {
			if (!reader.read_pod(spark.animation_frame) ||
				!reader.read_vec3_half(spark.start_position) ||
				!reader.read_vec3_half(spark.plane_normal)) {
				return false;
			}
			spark.position = mxt_super_spark_position_at_frame(
				spark.start_position, spark.final_position, spark.plane_normal, spark.animation_frame, spark.collectable);
			if (spark.animation_frame > 0) {
				spark.prev_position = mxt_super_spark_position_at_frame(
					spark.start_position, spark.final_position, spark.plane_normal, spark.animation_frame - 1, spark.collectable);
			} else {
				spark.prev_position = spark.position;
			}
		}
	}
	super_sparks = super_spark_state->sparks;

#define READ_NET_SCALAR(type, name) \
	for (int i = 0; i < num_cars; ++i) { \
		PhysicsCarSoA& soa = *cars[i].soa; \
		const int lane = cars[i].soa_index; \
		if constexpr (std::is_same_v<type, float>) { \
			float wire_value = 0.0f; \
			if (!reader.read_float16(wire_value)) return false; \
			soa.name[lane] = wire_value; \
		} else { \
			type wire_value; \
			if (!reader.read_pod(wire_value)) return false; \
			soa.name[lane] = wire_value; \
		} \
	}
	MXT_NET_CAR_SCALAR_FIELDS(READ_NET_SCALAR)
#undef READ_NET_SCALAR

#define READ_NET_BUMPER_SCALAR(type, name) \
	for (int i = 0; i < bumper_count; ++i) { \
		PhysicsCarSoA& soa = *bumper_cars[i].soa; \
		const int lane = bumper_cars[i].soa_index; \
		if constexpr (std::is_same_v<type, float>) { \
			float wire_value = 0.0f; \
			if (!reader.read_float16(wire_value)) return false; \
			soa.name[lane] = wire_value; \
		} else { \
			type wire_value; \
			if (!reader.read_pod(wire_value)) return false; \
			soa.name[lane] = wire_value; \
		} \
	}
	if (bumper_count > 0 && !bumper_cars) { \
		return false; \
	}
	if (bumper_cars) { \
		MXT_NET_CAR_SCALAR_FIELDS(READ_NET_BUMPER_SCALAR) \
	}
#undef READ_NET_BUMPER_SCALAR

	for (int i = 0; i < num_cars; ++i) {
		PhysicsCarSoA& soa = *cars[i].soa;
		soa.simulation_tick[cars[i].soa_index] = static_cast<uint32_t>(target_tick);
	}
	if (bumper_cars) {
		for (int i = 0; i < bumper_count; ++i) {
			PhysicsCarSoA& soa = *bumper_cars[i].soa;
			soa.simulation_tick[bumper_cars[i].soa_index] = static_cast<uint32_t>(target_tick);
		}
	}

	if (current_track) {
		for (int i = 0; i < num_cars; ++i) {
			PhysicsCarSoA& soa = *cars[i].soa;
			const int lane = cars[i].soa_index;
			if (soa.current_checkpoint[lane] >= current_track->num_checkpoints ||
					soa.current_collision_checkpoint[lane] < -1 ||
					soa.current_collision_checkpoint[lane] >= current_track->num_checkpoints ||
					soa.last_ground_checkpoint[lane] >= current_track->num_checkpoints) {
				godot::UtilityFunctions::printerr(
					godot::String("MXT network state rejected invalid checkpoint car="), static_cast<int64_t>(i),
					godot::String(" cp="), static_cast<int64_t>(soa.current_checkpoint[lane]),
					godot::String(" coll_cp="), static_cast<int64_t>(soa.current_collision_checkpoint[lane]),
					godot::String(" last_ground_cp="), static_cast<int64_t>(soa.last_ground_checkpoint[lane]),
					godot::String(" checkpoint_count="), static_cast<int64_t>(current_track->num_checkpoints));
				return false;
			}
		}
		for (int i = 0; bumper_cars && i < bumper_count; ++i) {
			PhysicsCarSoA& soa = *bumper_cars[i].soa;
			const int lane = bumper_cars[i].soa_index;
			if (bumper_states[i].active &&
					(soa.current_checkpoint[lane] >= current_track->num_checkpoints ||
					soa.current_collision_checkpoint[lane] < -1 ||
					soa.current_collision_checkpoint[lane] >= current_track->num_checkpoints ||
					soa.last_ground_checkpoint[lane] >= current_track->num_checkpoints)) {
				godot::UtilityFunctions::printerr(
					godot::String("MXT network state rejected invalid bumper checkpoint car="), static_cast<int64_t>(i),
					godot::String(" cp="), static_cast<int64_t>(soa.current_checkpoint[lane]),
					godot::String(" coll_cp="), static_cast<int64_t>(soa.current_collision_checkpoint[lane]),
					godot::String(" last_ground_cp="), static_cast<int64_t>(soa.last_ground_checkpoint[lane]),
					godot::String(" checkpoint_count="), static_cast<int64_t>(current_track->num_checkpoints));
				return false;
			}
		}
	}

#define READ_NET_VEC3_COMPONENT(name, component) \
	for (int i = 0; i < num_cars; ++i) { \
		PhysicsCarSoA& soa = *cars[i].soa; \
		const int lane = cars[i].soa_index; \
		if (!reader.read_pod(soa.name##_##component[lane])) return false; \
	}
#define READ_NET_VEC3(name) \
	READ_NET_VEC3_COMPONENT(name, x) \
	READ_NET_VEC3_COMPONENT(name, y) \
	READ_NET_VEC3_COMPONENT(name, z)
	MXT_NET_CAR_VEC3_FIELDS(READ_NET_VEC3)
#undef READ_NET_VEC3
#undef READ_NET_VEC3_COMPONENT
	for (int i = 0; i < num_cars; ++i) {
		PhysicsCarSoA& soa = *cars[i].soa;
		const int lane = cars[i].soa_index;
		const SimTransform surface = mxt_net_checkpoint_surface_or_identity(current_track, soa.current_checkpoint[lane], soa.checkpoint_fraction[lane]);
		SimVec3 local_current;
		SimVec3 local_old;
		if (!reader.read_vec3_half(local_current) || !reader.read_vec3_half(local_old)) {
			return false;
		}
		STORE_INDEXED_VEC3(soa, position_current, lane, surface.xform(local_current));
		STORE_INDEXED_VEC3(soa, position_old, lane, surface.xform(local_old));
	}
	for (int i = 0; i < num_cars; ++i) {
		PhysicsCarSoA& soa = *cars[i].soa;
		const int lane = cars[i].soa_index;
#define READ_NET_VEC3_HALF(name) do { SimVec3 v; if (!reader.read_vec3_half(v)) return false; STORE_INDEXED_VEC3(soa, name, lane, v); } while (0);
		MXT_NET_CAR_VEC3_HALF_FIELDS(READ_NET_VEC3_HALF)
#undef READ_NET_VEC3_HALF
	}

#define READ_NET_BUMPER_VEC3_COMPONENT(name, component) \
	for (int i = 0; i < bumper_count; ++i) { \
		PhysicsCarSoA& soa = *bumper_cars[i].soa; \
		const int lane = bumper_cars[i].soa_index; \
		if (!reader.read_pod(soa.name##_##component[lane])) return false; \
	}
#define READ_NET_BUMPER_VEC3(name) \
	READ_NET_BUMPER_VEC3_COMPONENT(name, x) \
	READ_NET_BUMPER_VEC3_COMPONENT(name, y) \
	READ_NET_BUMPER_VEC3_COMPONENT(name, z)
	if (bumper_cars) { \
		MXT_NET_CAR_VEC3_FIELDS(READ_NET_BUMPER_VEC3) \
	}
#undef READ_NET_BUMPER_VEC3
#undef READ_NET_BUMPER_VEC3_COMPONENT
	if (bumper_cars) {
		for (int i = 0; i < bumper_count; ++i) {
			PhysicsCarSoA& soa = *bumper_cars[i].soa;
			const int lane = bumper_cars[i].soa_index;
			const SimTransform surface = mxt_net_checkpoint_surface_or_identity(current_track, soa.current_checkpoint[lane], soa.checkpoint_fraction[lane]);
			SimVec3 local_current;
			SimVec3 local_old;
			if (!reader.read_vec3_half(local_current) || !reader.read_vec3_half(local_old)) {
				return false;
			}
			STORE_INDEXED_VEC3(soa, position_current, lane, surface.xform(local_current));
			STORE_INDEXED_VEC3(soa, position_old, lane, surface.xform(local_old));
		}
	}
	if (bumper_cars) {
		for (int i = 0; i < bumper_count; ++i) {
			PhysicsCarSoA& soa = *bumper_cars[i].soa;
			const int lane = bumper_cars[i].soa_index;
#define READ_NET_BUMPER_VEC3_HALF(name) do { SimVec3 v; if (!reader.read_vec3_half(v)) return false; STORE_INDEXED_VEC3(soa, name, lane, v); } while (0);
			MXT_NET_CAR_VEC3_HALF_FIELDS(READ_NET_BUMPER_VEC3_HALF)
#undef READ_NET_BUMPER_VEC3_HALF
		}
	}

	for (int i = 0; i < num_cars; ++i) {
		PhysicsCarSoA& soa = *cars[i].soa;
		const int lane = cars[i].soa_index;
		STORE_INDEXED_VEC3(soa, position_old_2, lane, LOAD_INDEXED_VEC3(soa, position_current, lane));
		STORE_INDEXED_VEC3(soa, position_old_dupe, lane, LOAD_INDEXED_VEC3(soa, position_old, lane));
	}
	if (bumper_cars) {
		for (int i = 0; i < bumper_count; ++i) {
			PhysicsCarSoA& soa = *bumper_cars[i].soa;
			const int lane = bumper_cars[i].soa_index;
			STORE_INDEXED_VEC3(soa, position_old_2, lane, LOAD_INDEXED_VEC3(soa, position_current, lane));
			STORE_INDEXED_VEC3(soa, position_old_dupe, lane, LOAD_INDEXED_VEC3(soa, position_old, lane));
		}
	}

#define READ_NET_TRANSFORM_COMPONENT(name, component) \
	for (int i = 0; i < num_cars; ++i) { \
		PhysicsCarSoA& soa = *cars[i].soa; \
		const int lane = cars[i].soa_index; \
		if (!reader.read_pod(soa.name##_##component[lane])) return false; \
	}
#define READ_NET_TRANSFORM(name) \
	READ_NET_TRANSFORM_COMPONENT(name, c0x) \
	READ_NET_TRANSFORM_COMPONENT(name, c0y) \
	READ_NET_TRANSFORM_COMPONENT(name, c0z) \
	READ_NET_TRANSFORM_COMPONENT(name, c1x) \
	READ_NET_TRANSFORM_COMPONENT(name, c1y) \
	READ_NET_TRANSFORM_COMPONENT(name, c1z) \
	READ_NET_TRANSFORM_COMPONENT(name, c2x) \
	READ_NET_TRANSFORM_COMPONENT(name, c2y) \
	READ_NET_TRANSFORM_COMPONENT(name, c2z) \
	READ_NET_TRANSFORM_COMPONENT(name, ox) \
	READ_NET_TRANSFORM_COMPONENT(name, oy) \
	READ_NET_TRANSFORM_COMPONENT(name, oz)
	MXT_NET_CAR_TRANSFORM_FIELDS(READ_NET_TRANSFORM)
#undef READ_NET_TRANSFORM
#undef READ_NET_TRANSFORM_COMPONENT

#define READ_NET_BUMPER_TRANSFORM_COMPONENT(name, component) \
	for (int i = 0; i < bumper_count; ++i) { \
		PhysicsCarSoA& soa = *bumper_cars[i].soa; \
		const int lane = bumper_cars[i].soa_index; \
		if (!reader.read_pod(soa.name##_##component[lane])) return false; \
	}
#define READ_NET_BUMPER_TRANSFORM(name) \
	READ_NET_BUMPER_TRANSFORM_COMPONENT(name, c0x) \
	READ_NET_BUMPER_TRANSFORM_COMPONENT(name, c0y) \
	READ_NET_BUMPER_TRANSFORM_COMPONENT(name, c0z) \
	READ_NET_BUMPER_TRANSFORM_COMPONENT(name, c1x) \
	READ_NET_BUMPER_TRANSFORM_COMPONENT(name, c1y) \
	READ_NET_BUMPER_TRANSFORM_COMPONENT(name, c1z) \
	READ_NET_BUMPER_TRANSFORM_COMPONENT(name, c2x) \
	READ_NET_BUMPER_TRANSFORM_COMPONENT(name, c2y) \
	READ_NET_BUMPER_TRANSFORM_COMPONENT(name, c2z) \
	READ_NET_BUMPER_TRANSFORM_COMPONENT(name, ox) \
	READ_NET_BUMPER_TRANSFORM_COMPONENT(name, oy) \
	READ_NET_BUMPER_TRANSFORM_COMPONENT(name, oz)
	if (bumper_cars) { \
		MXT_NET_CAR_TRANSFORM_FIELDS(READ_NET_BUMPER_TRANSFORM) \
	}
#undef READ_NET_BUMPER_TRANSFORM
#undef READ_NET_BUMPER_TRANSFORM_COMPONENT

#define READ_NET_BASIS(name) \
	for (int i = 0; i < num_cars; ++i) { \
		PhysicsCarSoA& soa = *cars[i].soa; \
		const int lane = cars[i].soa_index; \
		SimQuat q; \
		if (!reader.read_quat_i16(q)) return false; \
		SimTransform t = MXT_LOAD_TRANSFORM(soa, name, lane); \
		t.basis = SimBasis(q); \
		MXT_STORE_TRANSFORM(soa, name, lane, t); \
		soa.name##_ox[lane] = 0.0f; \
		soa.name##_oy[lane] = 0.0f; \
		soa.name##_oz[lane] = 0.0f; \
	}
	READ_NET_BASIS(basis_physical)
	READ_NET_BASIS(basis_physical_other)
#undef READ_NET_BASIS

#define READ_NET_BUMPER_BASIS(name) \
	for (int i = 0; i < bumper_count; ++i) { \
		PhysicsCarSoA& soa = *bumper_cars[i].soa; \
		const int lane = bumper_cars[i].soa_index; \
		SimQuat q; \
		if (!reader.read_quat_i16(q)) return false; \
		SimTransform t = MXT_LOAD_TRANSFORM(soa, name, lane); \
		t.basis = SimBasis(q); \
		MXT_STORE_TRANSFORM(soa, name, lane, t); \
		soa.name##_ox[lane] = 0.0f; \
		soa.name##_oy[lane] = 0.0f; \
		soa.name##_oz[lane] = 0.0f; \
	}
	if (bumper_cars) { \
		READ_NET_BUMPER_BASIS(basis_physical) \
		READ_NET_BUMPER_BASIS(basis_physical_other) \
	}
#undef READ_NET_BUMPER_BASIS

	TrackQueryScratch collision_old_scratch;
	for (int i = 0; i < num_cars; ++i) {
		collision_old_scratch.reset_mesh_query();
		collision_old_scratch.debug_mesh_current_global_car_index = i;
		cars[i].sample_old_corner_collision_surface(collision_old_scratch);
	}
	if (bumper_cars) {
		for (int i = 0; i < bumper_count; ++i) {
			collision_old_scratch.reset_mesh_query();
			collision_old_scratch.debug_mesh_current_global_car_index = num_cars + i;
			bumper_cars[i].sample_old_corner_collision_surface(collision_old_scratch);
		}
	}
	collision_old_scratch.debug_mesh_current_global_car_index = -1;

	for (int i = 0; i < num_cars; ++i) {
		PhysicsCarSoA& soa = *cars[i].soa;
		const int lane = cars[i].soa_index;
		if (soa.restore_state[lane] != 0) {
			SimTransform restore_start;
			SimTransform restore_target;
			if (!reader.read_transform(restore_start) ||
				!reader.read_transform(restore_target)) {
				return false;
			}
			MXT_STORE_TRANSFORM(soa, restore_start_transform, lane, restore_start);
			MXT_STORE_TRANSFORM(soa, restore_target_transform, lane, restore_target);
		} else {
			const SimTransform basis = MXT_LOAD_TRANSFORM(soa, basis_physical, lane);
			MXT_STORE_TRANSFORM(soa, restore_start_transform, lane, basis);
			MXT_STORE_TRANSFORM(soa, restore_target_transform, lane, basis);
		}
	}
	if (bumper_cars) {
		for (int i = 0; i < bumper_count; ++i) {
			PhysicsCarSoA& soa = *bumper_cars[i].soa;
			const int lane = bumper_cars[i].soa_index;
			if (soa.restore_state[lane] != 0) {
				SimTransform restore_start;
				SimTransform restore_target;
				if (!reader.read_transform(restore_start) ||
					!reader.read_transform(restore_target)) {
					return false;
				}
				MXT_STORE_TRANSFORM(soa, restore_start_transform, lane, restore_start);
				MXT_STORE_TRANSFORM(soa, restore_target_transform, lane, restore_target);
			} else {
				const SimTransform basis = MXT_LOAD_TRANSFORM(soa, basis_physical, lane);
				MXT_STORE_TRANSFORM(soa, restore_start_transform, lane, basis);
				MXT_STORE_TRANSFORM(soa, restore_target_transform, lane, basis);
			}
		}
	}

#define READ_NET_TILT_SCALAR(type, name) \
	for (int point = 0; point < 4; ++point) { \
		for (int i = 0; i < num_cars; ++i) { \
			PhysicsCarSoA& soa = *cars[i].soa; \
			const int p = cars[i].soa_index * 4 + point; \
			if constexpr (std::is_same_v<type, float>) { \
				float wire_value = 0.0f; \
				if (!reader.read_float16(wire_value)) return false; \
				soa.tilt_##name[p] = wire_value; \
			} else { \
				type wire_value; \
				if (!reader.read_pod(wire_value)) return false; \
				soa.tilt_##name[p] = wire_value; \
			} \
		} \
	}
	MXT_NET_TILT_SCALAR_FIELDS(READ_NET_TILT_SCALAR)
#undef READ_NET_TILT_SCALAR

#define READ_NET_BUMPER_TILT_SCALAR(type, name) \
	for (int point = 0; point < 4; ++point) { \
		for (int i = 0; i < bumper_count; ++i) { \
			PhysicsCarSoA& soa = *bumper_cars[i].soa; \
			const int p = bumper_cars[i].soa_index * 4 + point; \
			if constexpr (std::is_same_v<type, float>) { \
				float wire_value = 0.0f; \
				if (!reader.read_float16(wire_value)) return false; \
				soa.tilt_##name[p] = wire_value; \
			} else { \
				type wire_value; \
				if (!reader.read_pod(wire_value)) return false; \
				soa.tilt_##name[p] = wire_value; \
			} \
		} \
	}
	if (bumper_cars) { \
		MXT_NET_TILT_SCALAR_FIELDS(READ_NET_BUMPER_TILT_SCALAR) \
	}
#undef READ_NET_BUMPER_TILT_SCALAR

#define READ_NET_TILT_VEC3_COMPONENT(name, component) \
	for (int point = 0; point < 4; ++point) { \
		for (int i = 0; i < num_cars; ++i) { \
			PhysicsCarSoA& soa = *cars[i].soa; \
			const int p = cars[i].soa_index * 4 + point; \
			if (!reader.read_pod(soa.tilt_##name##_##component[p])) return false; \
		} \
	}
#define READ_NET_TILT_VEC3(name) \
	READ_NET_TILT_VEC3_COMPONENT(name, x) \
	READ_NET_TILT_VEC3_COMPONENT(name, y) \
	READ_NET_TILT_VEC3_COMPONENT(name, z)
	MXT_NET_TILT_VEC3_FIELDS(READ_NET_TILT_VEC3)
#undef READ_NET_TILT_VEC3
#undef READ_NET_TILT_VEC3_COMPONENT

#define READ_NET_BUMPER_TILT_VEC3_COMPONENT(name, component) \
	for (int point = 0; point < 4; ++point) { \
		for (int i = 0; i < bumper_count; ++i) { \
			PhysicsCarSoA& soa = *bumper_cars[i].soa; \
			const int p = bumper_cars[i].soa_index * 4 + point; \
			if (!reader.read_pod(soa.tilt_##name##_##component[p])) return false; \
		} \
	}
#define READ_NET_BUMPER_TILT_VEC3(name) \
	READ_NET_BUMPER_TILT_VEC3_COMPONENT(name, x) \
	READ_NET_BUMPER_TILT_VEC3_COMPONENT(name, y) \
	READ_NET_BUMPER_TILT_VEC3_COMPONENT(name, z)
	if (bumper_cars) { \
		MXT_NET_TILT_VEC3_FIELDS(READ_NET_BUMPER_TILT_VEC3) \
	}
#undef READ_NET_BUMPER_TILT_VEC3
#undef READ_NET_BUMPER_TILT_VEC3_COMPONENT

#define READ_NET_WALL_VEC3_COMPONENT(name, component) \
	for (int point = 0; point < 4; ++point) { \
		for (int i = 0; i < num_cars; ++i) { \
			PhysicsCarSoA& soa = *cars[i].soa; \
			const int p = cars[i].soa_index * 4 + point; \
			if (!reader.read_pod(soa.wall_##name##_##component[p])) return false; \
		} \
	}
#define READ_NET_WALL_VEC3(name) \
	READ_NET_WALL_VEC3_COMPONENT(name, x) \
	READ_NET_WALL_VEC3_COMPONENT(name, y) \
	READ_NET_WALL_VEC3_COMPONENT(name, z)
	MXT_NET_WALL_VEC3_FIELDS(READ_NET_WALL_VEC3)
#undef READ_NET_WALL_VEC3
#undef READ_NET_WALL_VEC3_COMPONENT

#define READ_NET_BUMPER_WALL_VEC3_COMPONENT(name, component) \
	for (int point = 0; point < 4; ++point) { \
		for (int i = 0; i < bumper_count; ++i) { \
			PhysicsCarSoA& soa = *bumper_cars[i].soa; \
			const int p = bumper_cars[i].soa_index * 4 + point; \
			if (!reader.read_pod(soa.wall_##name##_##component[p])) return false; \
		} \
	}
#define READ_NET_BUMPER_WALL_VEC3(name) \
	READ_NET_BUMPER_WALL_VEC3_COMPONENT(name, x) \
	READ_NET_BUMPER_WALL_VEC3_COMPONENT(name, y) \
	READ_NET_BUMPER_WALL_VEC3_COMPONENT(name, z)
	if (bumper_cars) { \
		MXT_NET_WALL_VEC3_FIELDS(READ_NET_BUMPER_WALL_VEC3) \
	}
#undef READ_NET_BUMPER_WALL_VEC3
#undef READ_NET_BUMPER_WALL_VEC3_COMPONENT

	const int local_trigger_count = current_track ? current_track->num_trigger_colliders : 0;
	if (trigger_count != local_trigger_count) {
		return false;
	}
	for (int i = 0; i < trigger_count; ++i) {
		uint8_t exploded = 0;
		float heat = 0.0f;
		uint32_t last_activation_tick = 0;
		uint8_t has_last_activation = 0;
		if (!reader.read_pod(exploded) ||
			!reader.read_pod(heat) ||
			!reader.read_pod(last_activation_tick) ||
			!reader.read_pod(has_last_activation)) {
			return false;
		}
		TriggerCollider* trigger = current_track->trigger_colliders[i];
		if (!trigger) {
			continue;
		}
		if (trigger->type == TRIGGER_TYPE::MINE) {
			static_cast<Mine*>(trigger)->exploded = exploded != 0;
		} else if (trigger->type == TRIGGER_TYPE::DASHPLATE) {
			Dashplate* dash = static_cast<Dashplate*>(trigger);
			dash->heat = heat;
			dash->last_activation_tick = last_activation_tick;
			dash->has_last_activation = has_last_activation != 0;
		}
	}

	rebuild_static_state_after_network_load();
	for (int i = 0; i < bumper_count && i < BUMPER_POOL_SIZE; ++i) {
		if (!bumper_cars) {
			continue;
		}
		PhysicsCarSoA& soa = *bumper_cars[i].soa;
		const int lane = bumper_cars[i].soa_index;
		if (bumper_states[i].active) {
			soa.current_track[lane] = current_track;
			soa.restore_state[lane] = 0;
			soa.restore_wait_frames[lane] = 0;
			soa.restore_move_frames[lane] = 0;
		} else {
			deactivate_bumper_car(i);
		}
	}
	if (current_track) {
		for (int i = 0; i < num_cars; ++i) {
			PhysicsCarSoA& soa = *cars[i].soa;
			const int lane = cars[i].soa_index;
			if (soa.current_checkpoint[lane] >= current_track->num_checkpoints ||
					soa.current_collision_checkpoint[lane] < -1 ||
					soa.current_collision_checkpoint[lane] >= current_track->num_checkpoints ||
					soa.last_ground_checkpoint[lane] >= current_track->num_checkpoints) {
				godot::UtilityFunctions::printerr(
					godot::String("MXT network state rejected invalid rebuilt checkpoint car="), static_cast<int64_t>(i),
					godot::String(" cp="), static_cast<int64_t>(soa.current_checkpoint[lane]),
					godot::String(" coll_cp="), static_cast<int64_t>(soa.current_collision_checkpoint[lane]),
					godot::String(" last_ground_cp="), static_cast<int64_t>(soa.last_ground_checkpoint[lane]),
					godot::String(" checkpoint_count="), static_cast<int64_t>(current_track->num_checkpoints));
				return false;
			}
		}
	}
	const int index = target_tick % STATE_BUFFER_LEN;
	const int size = gamestate_data.get_size();
	if (state_buffer[index].data && size > 0) {
		std::memcpy(state_buffer[index].data, gamestate_data.heap_start, size);
		state_buffer[index].size = size;
		state_buffer[index].tick = target_tick;
		save_bumper_states_to_saved_state(state_buffer[index]);
		update_saved_voice_transforms(state_buffer[index]);
	}
	return true;
}

void GameSim::rebuild_static_state_after_network_load() {
	const int rebuild_count = num_cars + (bumper_cars ? bumper_count : 0);
	for (int i = 0; i < rebuild_count; ++i) {
		PhysicsCar& car = i < num_cars ? cars[i] : bumper_cars[i - num_cars];
		PhysicsCarSoA& soa = *car.soa;
		const int lane = car.soa_index;
		car.update_machine_stats();
		if (soa.car_properties[lane]) {
			soa.calced_max_energy[lane] = soa.car_properties[lane]->max_energy + soa.ko_energy_bonus[lane];
		}
		soa.weight_derived_1[lane] = 52.0f * soa.stat_weight[lane] * 0.0625f;
		soa.weight_derived_2[lane] = 45.0f * soa.stat_weight[lane] * 0.0625f;
		soa.weight_derived_3[lane] = 52.0f * soa.stat_weight[lane] * 0.0625f;

		const SimTransform basis = MXT_LOAD_TRANSFORM(soa, basis_physical, lane);
		const SimVec3 position = LOAD_INDEXED_VEC3(soa, position_current, lane);
		STORE_INDEXED_VEC3(soa, position_collision_snapshot, lane, position);
		STORE_INDEXED_VEC3(soa, position_bottom, lane, basis.basis.xform(SimVec3(0.0f, -0.1f, 0.0f)) + position);
		STORE_INDEXED_VEC3(soa, position_behind, lane, basis.basis.xform(SimVec3(0.0f, 0.5f, 0.5f)) + position);
		STORE_INDEXED_VEC3(soa, track_surface_normal_prev, lane, LOAD_INDEXED_VEC3(soa, track_surface_normal, lane));
		STORE_INDEXED_VEC3(soa, velocity_local, lane, SimVec3());
		STORE_INDEXED_VEC3(soa, velocity_local_flattened_and_rotated, lane, SimVec3());
		STORE_INDEXED_VEC3(soa, collision_push_track, lane, SimVec3());
		STORE_INDEXED_VEC3(soa, collision_push_rail, lane, SimVec3());
		STORE_INDEXED_VEC3(soa, collision_push_total, lane, SimVec3());
		STORE_INDEXED_VEC3(soa, collision_response, lane, SimVec3());
		STORE_INDEXED_VEC3(soa, unk_vec3_0x4e4, lane, SimVec3());
		STORE_INDEXED_VEC3(soa, unk_vec3_0x4f0, lane, SimVec3());
		soa.input_steer_pitch[lane] = 0.0f;
		soa.input_strafe[lane] = 0.0f;
		soa.input_steer_yaw[lane] = 0.0f;
		soa.input_brake[lane] = 0.0f;
		soa.input_yaw_dupe[lane] = 0.0f;
		soa.terrain_state_2[lane] = 0;
		soa.suspension_reset_flag[lane] = 0;

		const SimVec3 velocity = LOAD_INDEXED_VEC3(soa, velocity, lane);
		if (std::abs(soa.stat_weight[lane]) > 0.0001f) {
			soa.speed_kmh[lane] = 216.0f * (velocity.length() / soa.stat_weight[lane]);
		} else {
			soa.speed_kmh[lane] = 0.0f;
		}

		soa.lap_progress[lane] = 0.0f;
		soa.checkpoint_track_distance[lane] = 0.0f;
		RaceTrack* track = soa.current_track[lane] ? soa.current_track[lane] : current_track;
		if (track &&
			soa.current_checkpoint[lane] >= 0 &&
			soa.current_checkpoint[lane] < track->num_checkpoints) {
			const CollisionCheckpoint& cp = track->checkpoints[soa.current_checkpoint[lane]];
			const float fraction = std::clamp(soa.checkpoint_fraction[lane], 0.0f, 1.0f);
			soa.lap_progress[lane] =
				(static_cast<float>(soa.current_checkpoint[lane]) + fraction) / static_cast<float>(track->num_checkpoints);
			float ground_distance = cp.distance - cp.local_distance + cp.local_distance * fraction;
			float lap_length = track->lap_length;
			if (lap_length <= 0.0f && track->num_checkpoints > 0) {
				lap_length = track->checkpoints[track->num_checkpoints - 1].distance;
			}
			if (lap_length > 0.0f) {
				ground_distance = std::fmod(ground_distance, lap_length);
				if (ground_distance < 0.0f) {
					ground_distance += lap_length;
				}
			}
			soa.checkpoint_track_distance[lane] = ground_distance;
		}

		RoadData& road = soa.road_sample[lane];
		road = RoadData();
		road.cp_idx = static_cast<int16_t>(soa.current_checkpoint[lane]);
		if (track &&
			soa.current_checkpoint[lane] >= 0 &&
			soa.current_checkpoint[lane] < track->num_checkpoints) {
			track->get_road_surface(soa.current_checkpoint[lane], position, road.road_t, road.spatial_t, road.closest_surface);
			STORE_INDEXED_VEC3(soa, track_surface_pos, lane, road.closest_surface.origin);
			road.closest_surface.basis[1] = LOAD_INDEXED_VEC3(soa, track_surface_normal, lane);
		} else {
			road.closest_surface = basis;
			road.closest_surface.origin = position;
			STORE_INDEXED_VEC3(soa, track_surface_pos, lane, position);
			road.closest_surface.basis[1] = LOAD_INDEXED_VEC3(soa, track_surface_normal, lane);
		}
		STORE_INDEXED_VEC3(soa, track_surface_normal_prev, lane, LOAD_INDEXED_VEC3(soa, track_surface_normal, lane));

		const SimTransform previous_basis = MXT_LOAD_TRANSFORM(soa, basis_physical_other, lane);
		const SimVec3 previous_position = LOAD_INDEXED_VEC3(soa, position_old, lane);
		const int point_base = lane * 4;
		const SimFloat4 tilt_x = sim_load4(soa.tilt_offset_x + point_base);
		const SimFloat4 tilt_y =
			sim_load4(soa.tilt_offset_y + point_base) +
			sim_load4(soa.tilt_force + point_base) -
			sim_load4(soa.tilt_rest_length + point_base);
		const SimFloat4 tilt_z = sim_load4(soa.tilt_offset_z + point_base);
		const SimVec3x4 tilt_pos_old = transform_points_components4(
			previous_basis.basis.c0.x, previous_basis.basis.c0.y, previous_basis.basis.c0.z,
			previous_basis.basis.c1.x, previous_basis.basis.c1.y, previous_basis.basis.c1.z,
			previous_basis.basis.c2.x, previous_basis.basis.c2.y, previous_basis.basis.c2.z,
			previous_position.x, previous_position.y, previous_position.z,
			tilt_x, tilt_y, tilt_z);
		const SimVec3x4 tilt_pos = transform_points_components4(
			basis.basis.c0.x, basis.basis.c0.y, basis.basis.c0.z,
			basis.basis.c1.x, basis.basis.c1.y, basis.basis.c1.z,
			basis.basis.c2.x, basis.basis.c2.y, basis.basis.c2.z,
			position.x, position.y, position.z,
			tilt_x, tilt_y, tilt_z);
		sim_store4(soa.tilt_pos_old_x + point_base, tilt_pos_old.x);
		sim_store4(soa.tilt_pos_old_y + point_base, tilt_pos_old.y);
		sim_store4(soa.tilt_pos_old_z + point_base, tilt_pos_old.z);
		sim_store4(soa.tilt_pos_x + point_base, tilt_pos.x);
		sim_store4(soa.tilt_pos_y + point_base, tilt_pos.y);
		sim_store4(soa.tilt_pos_z + point_base, tilt_pos.z);

		const SimFloat4 wall_x = sim_load4(soa.wall_offset_x + point_base);
		const SimFloat4 wall_y = sim_load4(soa.wall_offset_y + point_base);
		const SimFloat4 wall_z = sim_load4(soa.wall_offset_z + point_base);
		const SimVec3x4 wall_pos_old = transform_points_components4(
			previous_basis.basis.c0.x, previous_basis.basis.c0.y, previous_basis.basis.c0.z,
			previous_basis.basis.c1.x, previous_basis.basis.c1.y, previous_basis.basis.c1.z,
			previous_basis.basis.c2.x, previous_basis.basis.c2.y, previous_basis.basis.c2.z,
			previous_position.x, previous_position.y, previous_position.z,
			wall_x, wall_y, wall_z);
		const SimVec3x4 wall_pos = transform_points_components4(
			basis.basis.c0.x, basis.basis.c0.y, basis.basis.c0.z,
			basis.basis.c1.x, basis.basis.c1.y, basis.basis.c1.z,
			basis.basis.c2.x, basis.basis.c2.y, basis.basis.c2.z,
			position.x, position.y, position.z,
			wall_x, wall_y, wall_z);
		sim_store4(soa.wall_pos_a_x + point_base, wall_pos_old.x);
		sim_store4(soa.wall_pos_a_y + point_base, wall_pos_old.y);
		sim_store4(soa.wall_pos_a_z + point_base, wall_pos_old.z);
		sim_store4(soa.wall_pos_b_x + point_base, wall_pos.x);
		sim_store4(soa.wall_pos_b_y + point_base, wall_pos.y);
		sim_store4(soa.wall_pos_b_z + point_base, wall_pos.z);

		for (int point = 0; point < 4; ++point) {
			const int p = point_base + point;
			soa.tilt_force_at_point[p] = 0.0f;
			soa.tilt_force_spatial_len[p] = 0.0f;
			STORE_INDEXED_VEC3(soa, tilt_target_dir, p, SimVec3());
			STORE_INDEXED_VEC3(soa, tilt_force_spatial, p, SimVec3());
			SimVec3 tilt_up = LOAD_INDEXED_VEC3(soa, track_surface_normal, lane);
			if (tilt_up.length_squared() <= 0.0001f) {
				tilt_up = basis.basis.get_column(1);
			}
			if (tilt_up.length_squared() <= 0.0001f) {
				tilt_up = SimVec3(0.0f, 1.0f, 0.0f);
			}
			tilt_up.normalize();
			STORE_INDEXED_VEC3(soa, tilt_up_vector, p, tilt_up);
			STORE_INDEXED_VEC3(soa, tilt_up_vector_2, p, tilt_up);
			STORE_INDEXED_VEC3(soa, wall_collision, p, SimVec3());
		}
	}
}

godot::PackedByteArray GameSim::get_state_data(int target_tick) const {
	int index = target_tick % STATE_BUFFER_LEN;
	if (!state_buffer[index].data)
		return godot::PackedByteArray();
	return serialize_network_state(target_tick);
}

godot::Dictionary GameSim::get_network_state_size_stats() const {
	const NetworkStateSizeStats& stats = last_network_state_size_stats;
	godot::Dictionary out;
	out["total"] = stats.total;
	out["header"] = stats.header;
	out["bumper_meta"] = stats.bumper_meta;
	out["sparks"] = stats.sparks;
	out["car_scalars"] = stats.car_scalars;
	out["bumper_scalars"] = stats.bumper_scalars;
	out["car_vec3"] = stats.car_vec3;
	out["bumper_vec3"] = stats.bumper_vec3;
	out["car_transform"] = stats.car_transform;
	out["bumper_transform"] = stats.bumper_transform;
	out["car_basis"] = stats.car_basis;
	out["bumper_basis"] = stats.bumper_basis;
	out["car_conditionals"] = stats.car_conditionals;
	out["bumper_conditionals"] = stats.bumper_conditionals;
	out["car_tilt"] = stats.car_tilt;
	out["bumper_tilt"] = stats.bumper_tilt;
	out["car_wall"] = stats.car_wall;
	out["bumper_wall"] = stats.bumper_wall;
	out["triggers"] = stats.triggers;
	out["car_collision_old_count"] = stats.car_collision_old_count;
	out["bumper_collision_old_count"] = stats.bumper_collision_old_count;
	out["car_restore_count"] = stats.car_restore_count;
	out["bumper_restore_count"] = stats.bumper_restore_count;
	out["active_bumper_count"] = stats.active_bumper_count;
	out["active_spark_count"] = stats.active_spark_count;
	out["trigger_count"] = stats.trigger_count;
	out["car_count"] = stats.car_count;
	out["bumper_count"] = stats.bumper_count;
	return out;
}

bool GameSim::load_state_data(int target_tick, godot::PackedByteArray data) {
	if (data.size() >= static_cast<int>(sizeof(uint32_t))) {
		uint32_t magic = 0;
		std::memcpy(&magic, data.ptr(), sizeof(uint32_t));
		if (magic == MXT_NET_STATE_MAGIC) {
			if (!deserialize_network_state(target_tick, data)) {
				godot::UtilityFunctions::printerr(godot::String("MXT load_state_data failed to deserialize network state"));
				return false;
			}
			tick = target_tick + 1;
			return true;
		}
	}
	set_state_data(target_tick, data);
	load_state(target_tick);
	return true;
}

void GameSim::set_state_data(int target_tick, godot::PackedByteArray data) {
	int index = target_tick % STATE_BUFFER_LEN;
	if (!state_buffer[index].data)
		return;
	if (data.size() >= static_cast<int>(sizeof(uint32_t))) {
		uint32_t magic = 0;
		std::memcpy(&magic, data.ptr(), sizeof(uint32_t));
		if (magic == MXT_NET_STATE_MAGIC) {
			const int live_size = gamestate_data.get_size();
			BumperState live_bumper_states[BUMPER_POOL_SIZE];
			const uint8_t live_bumper_scheduler_lap = bumper_scheduler_lap;
			const uint32_t live_bumper_next_sequence = bumper_next_sequence;
			for (int i = 0; i < BUMPER_POOL_SIZE; ++i) {
				live_bumper_states[i] = bumper_states[i];
			}
			if (live_size > 0) {
				if (static_cast<int>(network_state_live_backup.size()) < live_size) {
					network_state_live_backup.resize(static_cast<size_t>(live_size));
				}
				std::memcpy(network_state_live_backup.data(), gamestate_data.heap_start, static_cast<size_t>(live_size));
			}
			if (!deserialize_network_state(target_tick, data)) {
				godot::UtilityFunctions::printerr(
					godot::String("MXT set_state_data failed to deserialize network state tick="),
					static_cast<int64_t>(target_tick));
			}
			if (live_size > 0 && static_cast<int>(network_state_live_backup.size()) >= live_size) {
				std::memcpy(gamestate_data.heap_start, network_state_live_backup.data(), static_cast<size_t>(live_size));
				gamestate_data.set_size(live_size);
				fix_pointers();
			}
			for (int i = 0; i < BUMPER_POOL_SIZE; ++i) {
				bumper_states[i] = live_bumper_states[i];
			}
			bumper_scheduler_lap = live_bumper_scheduler_lap;
			bumper_next_sequence = live_bumper_next_sequence;
			return;
		}
	}
	// game state never changes in size after instantiation
	// and should always be the same size between the server and all clients
	int size = static_cast<int>(data.size());
	if (size > 0) {
		memcpy(state_buffer[index].data, data.ptr(), size);
		state_buffer[index].size = size;
		state_buffer[index].tick = target_tick;
		state_buffer[index].voice_transform_count = 0;
	}
}

#undef MXT_NET_CAR_SCALAR_FIELDS
#undef MXT_NET_CAR_VEC3_FIELDS
#undef MXT_NET_CAR_VEC3_HALF_FIELDS
#undef MXT_NET_CAR_TRANSFORM_FIELDS
#undef MXT_NET_TILT_SCALAR_FIELDS
#undef MXT_NET_TILT_VEC3_FIELDS
#undef MXT_NET_WALL_VEC3_FIELDS

void GameSim::fix_pointers() {
	if (super_spark_state) {
		super_sparks = super_spark_state->sparks;
	} else {
		super_sparks = nullptr;
	}
	if (!sim_started || !cars) {
		return;
	}

	const int total_lane_count = (num_cars + 3) & ~3;
	for (int i = 0; i < total_lane_count; ++i) {
		cars[i].soa->current_track[cars[i].soa_index] = current_track;
		// TODO: machine_name is static metadata, not gamestate. Move it out of serialized SoA state.
		cars[i].soa->machine_name[cars[i].soa_index] = "Blue Falcon";
		if (car_properties_array) {
			cars[i].soa->car_properties[cars[i].soa_index] = &car_properties_array[i];
		}
	}
	if (bumper_cars && bumper_count > 0) {
		const int total_bumper_lane_count = (bumper_count + 3) & ~3;
		for (int i = 0; i < total_bumper_lane_count; ++i) {
			const int slot = i < bumper_count ? i : bumper_count - 1;
			PhysicsCarSoA& soa = *bumper_cars[i].soa;
			const int lane = bumper_cars[i].soa_index;
			soa.current_track[lane] = (i < bumper_count && bumper_states[slot].active) ? current_track : nullptr;
			soa.machine_name[lane] = "Bumper";
			if (bumper_properties_array) {
				soa.car_properties[lane] = &bumper_properties_array[i];
			}
		}
	}

	if (current_track && current_track->trigger_colliders) {
		for (int i = 0; i < current_track->num_trigger_colliders; ++i) {
			TriggerCollider* trig = current_track->trigger_colliders[i];

			TRIGGER_TYPE::TYPE type = trig->type;
			SimTransform transform = trig->transform;
			SimVec3 half_extents = trig->half_extents;
			SimTransform inv_transform = trig->inv_transform;
			int seg_idx = trig->segment_index;
			int cp_idx = trig->checkpoint_index;
			bool exploded = false;
			float dashplate_heat = 0.0f;
			uint32_t dashplate_last_tick = 0;
			bool dashplate_has_last_activation = false;
			if (type == TRIGGER_TYPE::MINE) {
				exploded = static_cast<Mine*>(trig)->exploded;
			}
			if (type == TRIGGER_TYPE::DASHPLATE) {
				Dashplate* dash = static_cast<Dashplate*>(trig);
				dashplate_heat = dash->heat;
				dashplate_last_tick = dash->last_activation_tick;
				dashplate_has_last_activation = dash->has_last_activation;
			}

			switch (type) {
			case TRIGGER_TYPE::DASHPLATE:
				new (trig) Dashplate();
				break;
			case TRIGGER_TYPE::JUMPPLATE:
				new (trig) Jumpplate();
				break;
			case TRIGGER_TYPE::MINE:
				new (trig) Mine();
				break;
			default:
				new (trig) TriggerCollider();
				break;
			}

			trig->transform = transform;
			trig->half_extents = half_extents;
			trig->inv_transform = inv_transform;
			trig->segment_index = seg_idx;
			trig->checkpoint_index = cp_idx;
			if (type == TRIGGER_TYPE::MINE) {
				static_cast<Mine*>(trig)->exploded = exploded;
			}
			if (type == TRIGGER_TYPE::DASHPLATE) {
				Dashplate* dash = static_cast<Dashplate*>(trig);
				dash->heat = dashplate_heat;
				dash->last_activation_tick = dashplate_last_tick;
				dash->has_last_activation = dashplate_has_last_activation;
			}

			current_track->trigger_colliders[i] = trig;
		}
	}
}
