#include "gamesim/gamesim_internal.h"

#include "godot_cpp/variant/utility_functions.hpp"
#include "core/math_utils.h"
#include "audio/spatial_audio_manager.h"
#include "track/road_embed.h"
#include "track/road_modulation.h"
#include "track/trigger_collider.h"

#include <algorithm>
#include <cfloat>
#include <cfenv>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <new>
#include <numeric>
#include <type_traits>
#include <vector>

#if defined(__SSE__)
#include <xmmintrin.h>
#endif

using namespace godot;

static bool invert_basis(const SimBasis& basis, SimBasis& out)
{
	const SimVec3 a = basis.c0;
	const SimVec3 b = basis.c1;
	const SimVec3 c = basis.c2;
	const SimVec3 row0 = b.cross(c);
	const float determinant = a.dot(row0);
	if (std::abs(determinant) <= 1.0e-8f) {
		out = basis.transposed();
		return false;
	}
	const float inverse_determinant = 1.0f / determinant;
	const SimVec3 r0 = row0 * inverse_determinant;
	const SimVec3 r1 = c.cross(a) * inverse_determinant;
	const SimVec3 r2 = a.cross(b) * inverse_determinant;
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

void GameSim::instantiate_gamesim(StreamPeerBuffer* lvldat_buf, godot::Array car_prop_buffers, godot::Array accel_settings)
{
	if (Engine::get_singleton()->is_editor_hint() && !performance_benchmark_mode) return;

	const int property_count = car_prop_buffers.size();
	std::vector<PhysicsCarProperties> sampled_car_properties(static_cast<size_t>(property_count));
	std::vector<float> sampled_machine_settings(static_cast<size_t>(property_count), 1.0f);
	for (int i = 0; i < property_count; ++i) {
		if (i < accel_settings.size() && accel_settings[i].get_type() == godot::Variant::FLOAT) {
			sampled_machine_settings[static_cast<size_t>(i)] =
				std::clamp(static_cast<float>(accel_settings[i]), 0.0f, 1.0f);
		}
		const godot::PackedByteArray bytes = car_prop_buffers[i];
		godot::String parse_error;
		if (!PhysicsCarProperties::deserialize_and_sample(
				bytes,
				sampled_machine_settings[static_cast<size_t>(i)],
				sampled_car_properties[static_cast<size_t>(i)],
				parse_error)) {
			UtilityFunctions::printerr(
				String("MXT car properties rejected for racer "),
				static_cast<int64_t>(i),
				String(": "),
				parse_error);
			return;
		}
	}

	tick = 0;
	race_events.clear();

	int32_t buffer_size = lvldat_buf->get_size();
	const int requested_cars_hint = car_prop_buffers.size() > 0 ? car_prop_buffers.size() : 1;

	const size_t level_heap_size = performance_benchmark_mode
		? std::max<size_t>(
			1024u * 1024u * 2u,
			static_cast<size_t>(buffer_size) * 4u + 1024u * 512u)
		: std::max<size_t>(
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
		state_buffer[i].data = performance_benchmark_mode
			? nullptr
			: static_cast<char*>(malloc(state_capacity));
		state_buffer[i].size = 0;
		state_buffer[i].tick = -1;
		state_buffer[i].voice_transform_count = 0;
		state_buffer[i].voice_transforms.resize(requested_cars_hint);
		state_buffer[i].car_local_state_size = 0;
		state_buffer[i].bumper_local_state_size = 0;
		state_buffer[i].vehicle_local_state.clear();
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
		current_track->segments[seg].road_shape->embed_terrain_mask = 0;

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
			current_track->segments[seg].road_shape->embed_terrain_mask |=
				static_cast<uint32_t>(current_track->segments[seg].road_shape->road_embeds[embed].embed_type);

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
		if (current_track->num_checkpoints > 0) {
			std::vector<int>& offsets = current_track->trigger_checkpoint_offsets;
			std::vector<int>& indices = current_track->trigger_checkpoint_indices;
			offsets.assign(static_cast<size_t>(current_track->num_checkpoints) + 1u, 0);
			auto append_relevant_checkpoint = [&](std::vector<int>& out, int cp_idx) {
				if (cp_idx < 0 || cp_idx >= current_track->num_checkpoints) {
					return;
				}
				for (int existing : out) {
					if (existing == cp_idx) {
						return;
					}
				}
				out.push_back(cp_idx);
			};
			std::vector<int> relevant_checkpoints;
			for (int t = 0; t < current_track->num_trigger_colliders; ++t) {
				TriggerCollider* trig = current_track->trigger_colliders[t];
				relevant_checkpoints.clear();
				append_relevant_checkpoint(relevant_checkpoints, trig->checkpoint_index);
				if (trig->checkpoint_index >= 0 && trig->checkpoint_index < current_track->num_checkpoints) {
					const CollisionCheckpoint& cp = current_track->checkpoints[trig->checkpoint_index];
					for (int n = 0; n < cp.num_neighboring_checkpoints; ++n) {
						append_relevant_checkpoint(relevant_checkpoints, cp.neighboring_checkpoints[n]);
					}
				}
				for (int cp_idx : relevant_checkpoints) {
					offsets[static_cast<size_t>(cp_idx) + 1u] += 1;
				}
			}
			for (int cp_idx = 1; cp_idx <= current_track->num_checkpoints; ++cp_idx) {
				offsets[cp_idx] += offsets[cp_idx - 1];
			}
			indices.assign(static_cast<size_t>(offsets[current_track->num_checkpoints]), 0);
			std::vector<int> write_offsets = offsets;
			for (int t = 0; t < current_track->num_trigger_colliders; ++t) {
				TriggerCollider* trig = current_track->trigger_colliders[t];
				relevant_checkpoints.clear();
				append_relevant_checkpoint(relevant_checkpoints, trig->checkpoint_index);
				if (trig->checkpoint_index >= 0 && trig->checkpoint_index < current_track->num_checkpoints) {
					const CollisionCheckpoint& cp = current_track->checkpoints[trig->checkpoint_index];
					for (int n = 0; n < cp.num_neighboring_checkpoints; ++n) {
						append_relevant_checkpoint(relevant_checkpoints, cp.neighboring_checkpoints[n]);
					}
				}
				for (int cp_idx : relevant_checkpoints) {
					indices[static_cast<size_t>(write_offsets[cp_idx]++)] = t;
				}
			}
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
			cars[i].soa->race_lap_target[cars[i].soa_index] = target_lap_count;
			if (i < property_count) {
				*(cars[i].soa->car_properties[cars[i].soa_index]) =
					sampled_car_properties[static_cast<size_t>(i)];
				cars[i].soa->m_accel_setting[cars[i].soa_index] =
					sampled_machine_settings[static_cast<size_t>(i)];
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
				const float checkpoint_fraction = checkpoint_plane_fraction_unclamped(cur_cp, spawn_transform.origin);
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
			for (int point = 0; point < 4; ++point) {
				const int p = point_base + point;
				car_soa->tilt_state[p] = 0;
				car_soa->tilt_force[p] = 0.0f;
				STORE_INDEXED_VEC3(*car_soa, tilt_force_spatial, p, SimVec3());
				STORE_INDEXED_VEC3(*car_soa, tilt_up_vector_2, p, spawn_up);
				STORE_INDEXED_VEC3(*car_soa, tilt_up_vector, p, spawn_up);
			}
			sim_store4(car_soa->tilt_pos_old_x + point_base, tilt_pos.x);
			sim_store4(car_soa->tilt_pos_old_y + point_base, tilt_pos.y);
			sim_store4(car_soa->tilt_pos_old_z + point_base, tilt_pos.z);
			sim_store4(car_soa->tilt_pos_x + point_base, tilt_pos.x);
			sim_store4(car_soa->tilt_pos_y + point_base, tilt_pos.y);
			sim_store4(car_soa->tilt_pos_z + point_base, tilt_pos.z);
			car_soa->machine_state[car_idx] &= ~(MACHINESTATE::AIRBORNE | MACHINESTATE::AIRBORNEMORE0_2S_Q | MACHINESTATE::JUSTLANDED);
			car_soa->air_time[car_idx] = 0;
		}

		for (int i = 0; i < bumper_count; ++i) {
			bumper_cars[i].soa->current_track[bumper_cars[i].soa_index] = current_track;
			bumper_cars[i].soa->race_lap_target[bumper_cars[i].soa_index] = 0;
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
		clear_trigger_visuals();
		reset_collision_spark_effects(true);
		reset_drift_plasma_effects(true);
		hide_finish_line_visual();
		spatial_audio_last_assignment_tick = -1;
		spatial_audio_last_update_frame = UINT64_MAX;
		if (spatial_audio_manager) {
			spatial_audio_manager->clear_all();
		}
		if (current_track) {
			current_track->num_trigger_colliders = 0;
			current_track->trigger_colliders = nullptr;
		}
		if (cars) {
			HeapHandler::free_physics_car_static_soa_arrays(cars, num_cars);
			::free(cars);
			cars = nullptr;
		}
		if (bumper_cars) {
			HeapHandler::free_physics_car_static_soa_arrays(bumper_cars, bumper_count);
			::free(bumper_cars);
			bumper_cars = nullptr;
		}
		if (car_properties_array) {
			::free(car_properties_array);
			car_properties_array = nullptr;
		}
		if (bumper_properties_array) {
			::free(bumper_properties_array);
			bumper_properties_array = nullptr;
		}
		if (level_data.is_live()) {
			level_data.free_heap();
		}
		if (gamestate_data.is_live()) {
			gamestate_data.free_heap();
		}
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
			state_buffer[i].voice_transforms.shrink_to_fit();
			state_buffer[i].car_local_state_size = 0;
			state_buffer[i].bumper_local_state_size = 0;
			state_buffer[i].vehicle_local_state.clear();
			state_buffer[i].vehicle_local_state.shrink_to_fit();
		}
		if (input_buffer) {
			::free(input_buffer);
			input_buffer = nullptr;
		}
		free_vehicle_tick_soa();
		network_state_live_backup.clear();
		network_state_live_backup.shrink_to_fit();
		sim_started = false;
		tick = 0;
		current_track = nullptr;
		if (car_player_ids) {
			::free(car_player_ids);
			car_player_ids = nullptr;
		}
		if (car_is_cpu) {
			::free(car_is_cpu);
			car_is_cpu = nullptr;
		}
		num_cars = 0;
		car_render_manager = nullptr;
		gameplay_camera_node = nullptr;
		render_camera_node = nullptr;
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
		clear_player_index_lookup();
		race_events.clear();
	};
