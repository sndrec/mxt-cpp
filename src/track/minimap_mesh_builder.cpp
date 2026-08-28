#include "track/minimap_mesh_builder.h"

#include "core/enums.h"
#include "track/racetrack.h"

#include "godot_cpp/classes/mesh.hpp"
#include "godot_cpp/variant/array.hpp"
#include "godot_cpp/variant/packed_vector3_array.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace {

enum MinimapSurface : int {
	MINIMAP_SURFACE_ROAD = 0,
	MINIMAP_SURFACE_RECHARGE,
	MINIMAP_SURFACE_DIRT,
	MINIMAP_SURFACE_ICE,
	MINIMAP_SURFACE_LAVA,
	MINIMAP_SURFACE_COUNT,
	MINIMAP_SURFACE_EXCLUDED = -1,
};

struct MinimapMeshData {
	std::vector<godot::Vector3> vertices[MINIMAP_SURFACE_COUNT];
};

static godot::Vector3 to_godot(const SimVec3& value)
{
	return godot::Vector3(value.x, value.y, value.z);
}

static int surface_for_terrain(uint32_t terrain, bool ordinary_surface)
{
	const uint32_t forbidden = TERRAIN::RAIL | TERRAIN::KILL | TERRAIN::FALL |
		TERRAIN::HOLE | TERRAIN::DASH | TERRAIN::JUMP;
	if ((terrain & forbidden) != 0u) {
		return MINIMAP_SURFACE_EXCLUDED;
	}
	if ((terrain & TERRAIN::LAVA) != 0u) {
		return MINIMAP_SURFACE_LAVA;
	}
	if ((terrain & TERRAIN::DIRT) != 0u) {
		return MINIMAP_SURFACE_DIRT;
	}
	if ((terrain & TERRAIN::ICE) != 0u) {
		return MINIMAP_SURFACE_ICE;
	}
	if ((terrain & TERRAIN::RECHARGE) != 0u) {
		return MINIMAP_SURFACE_RECHARGE;
	}
	const uint32_t remaining = terrain & ~static_cast<uint32_t>(TERRAIN::BACKSIDE);
	if (ordinary_surface && (remaining == 0u || remaining == TERRAIN::NORMAL)) {
		return MINIMAP_SURFACE_ROAD;
	}
	return MINIMAP_SURFACE_EXCLUDED;
}

static uint32_t analytic_terrain_at(const RoadShape& shape, float x, float y)
{
	for (int index = 0; index < shape.num_embeds; ++index) {
		const RoadEmbed& embed = shape.road_embeds[index];
		if (y < embed.start_offset || y > embed.end_offset ||
				embed.left_border == nullptr || embed.right_border == nullptr) {
			continue;
		}
		float left = embed.left_border->sample(y);
		float right = embed.right_border->sample(y);
		if (left > right) {
			std::swap(left, right);
		}
		if (x >= left && x <= right) {
			return static_cast<uint32_t>(embed.embed_type);
		}
	}
	return 0u;
}

static void append_triangle(
	std::vector<godot::Vector3>& vertices,
	const godot::Vector3& a,
	const godot::Vector3& b,
	const godot::Vector3& c)
{
	vertices.push_back(a);
	vertices.push_back(b);
	vertices.push_back(c);
}

static void append_quad(
	std::vector<godot::Vector3>& vertices,
	const godot::Vector3& a,
	const godot::Vector3& b,
	const godot::Vector3& c,
	const godot::Vector3& d)
{
	append_triangle(vertices, a, b, c);
	append_triangle(vertices, a, c, d);
}

static void append_curve_times(std::vector<float>& times, const Curve* curve)
{
	if (curve == nullptr || curve->keyframes == nullptr) {
		return;
	}
	for (int index = 0; index < curve->num_keyframes; ++index) {
		times.push_back(std::clamp(curve->keyframes[index].time, 0.0f, 1.0f));
	}
}

static std::vector<float> build_longitudinal_samples(const TrackSegment& segment)
{
	const int distance_steps = std::clamp(
		static_cast<int>(std::ceil(std::max(segment.segment_length, 1.0f) / 25.0f)),
		2,
		512);
	std::vector<float> times;
	times.reserve(static_cast<size_t>(distance_steps + 2 +
		(segment.curve_matrix ? segment.curve_matrix->num_keyframes : 0)));
	for (int step = 0; step <= distance_steps; ++step) {
		times.push_back(static_cast<float>(step) / static_cast<float>(distance_steps));
	}
	if (segment.curve_matrix != nullptr && segment.curve_matrix->times != nullptr) {
		for (int index = 0; index < segment.curve_matrix->num_keyframes; ++index) {
			times.push_back(std::clamp(segment.curve_matrix->times[index], 0.0f, 1.0f));
		}
	}
	if (segment.road_shape != nullptr) {
		for (int index = 0; index < segment.road_shape->num_embeds; ++index) {
			const RoadEmbed& embed = segment.road_shape->road_embeds[index];
			times.push_back(std::clamp(embed.start_offset, 0.0f, 1.0f));
			times.push_back(std::clamp(embed.end_offset, 0.0f, 1.0f));
			append_curve_times(times, embed.left_border);
			append_curve_times(times, embed.right_border);
		}
	}
	std::sort(times.begin(), times.end());
	times.erase(std::unique(times.begin(), times.end(), [](float a, float b) {
		return std::abs(a - b) <= 0.00001f;
	}), times.end());
	return times;
}

static int lateral_step_count(int shape_type)
{
	switch (shape_type) {
		case ROAD_SHAPE_TYPE::ROAD_SHAPE_FLAT:
		case ROAD_SHAPE_TYPE::ROAD_SHAPE_TUNNEL:
			return 24;
		default:
			return 32;
	}
}

static void append_analytic_segment(MinimapMeshData& data, const TrackSegment& segment)
{
	if (!segment.analytic_collision_enabled || segment.road_shape == nullptr ||
			segment.curve_matrix == nullptr) {
		return;
	}
	const RoadShape& shape = *segment.road_shape;
	const std::vector<float> longitudinal = build_longitudinal_samples(segment);
	if (longitudinal.size() < 2) {
		return;
	}
	const int lateral_steps = lateral_step_count(shape.shape_type);
	std::vector<godot::Vector3> previous(static_cast<size_t>(lateral_steps + 1));
	std::vector<godot::Vector3> current(static_cast<size_t>(lateral_steps + 1));

	auto fill_row = [&](std::vector<godot::Vector3>& row, float y) {
		for (int step = 0; step <= lateral_steps; ++step) {
			const float x = -1.0f + 2.0f * static_cast<float>(step) / static_cast<float>(lateral_steps);
			SimVec3 position;
			shape.get_position_at_time(position, SimVec2(x, y));
			row[static_cast<size_t>(step)] = to_godot(position);
		}
	};

	fill_row(previous, longitudinal.front());
	for (size_t row = 1; row < longitudinal.size(); ++row) {
		const float y0 = longitudinal[row - 1];
		const float y1 = longitudinal[row];
		fill_row(current, y1);
		for (int column = 0; column < lateral_steps; ++column) {
			const float x0 = -1.0f + 2.0f * static_cast<float>(column) / static_cast<float>(lateral_steps);
			const float x1 = -1.0f + 2.0f * static_cast<float>(column + 1) / static_cast<float>(lateral_steps);
			const uint32_t terrain = analytic_terrain_at(shape, 0.5f * (x0 + x1), 0.5f * (y0 + y1));
			const int surface = surface_for_terrain(terrain, true);
			if (surface == MINIMAP_SURFACE_EXCLUDED) {
				continue;
			}
			append_quad(
				data.vertices[surface],
				previous[static_cast<size_t>(column)],
				current[static_cast<size_t>(column)],
				current[static_cast<size_t>(column + 1)],
				previous[static_cast<size_t>(column + 1)]);
		}
		previous.swap(current);
	}
}

static void append_collision_triangle(MinimapMeshData& data, const TrackMeshCollisionTriangle& triangle)
{
	const int surface = surface_for_terrain(triangle.terrain, true);
	if (surface == MINIMAP_SURFACE_EXCLUDED) {
		return;
	}
	godot::Vector3 a = to_godot(triangle.p0);
	godot::Vector3 b = to_godot(triangle.p1);
	godot::Vector3 c = to_godot(triangle.p2);
	if (surface != MINIMAP_SURFACE_ROAD) {
		godot::Vector3 normal = to_godot(triangle.n0 + triangle.n1 + triangle.n2);
		if (normal.length_squared() > 0.000001f) {
			normal.normalize();
			a += normal * 0.05f;
			b += normal * 0.05f;
			c += normal * 0.05f;
		}
	}
	append_triangle(data.vertices[surface], a, b, c);
}

static godot::StringName surface_name(int surface)
{
	switch (surface) {
		case MINIMAP_SURFACE_RECHARGE: return godot::StringName("recharge");
		case MINIMAP_SURFACE_DIRT: return godot::StringName("dirt");
		case MINIMAP_SURFACE_ICE: return godot::StringName("ice");
		case MINIMAP_SURFACE_LAVA: return godot::StringName("lava");
		default: return godot::StringName("road");
	}
}

} // namespace

godot::Ref<godot::ArrayMesh> build_track_minimap_mesh(const RaceTrack& track)
{
	MinimapMeshData data;
	for (int segment = 0; segment < track.num_segments; ++segment) {
		append_analytic_segment(data, track.segments[segment]);
	}
	for (int triangle = 0; triangle < track.num_mesh_collision_triangles; ++triangle) {
		append_collision_triangle(data, track.mesh_collision_triangles[triangle]);
	}

	godot::Ref<godot::ArrayMesh> mesh;
	mesh.instantiate();
	for (int surface = 0; surface < MINIMAP_SURFACE_COUNT; ++surface) {
		const std::vector<godot::Vector3>& source = data.vertices[surface];
		if (source.empty()) {
			continue;
		}
		godot::PackedVector3Array vertices;
		vertices.resize(static_cast<int64_t>(source.size()));
		for (int64_t index = 0; index < static_cast<int64_t>(source.size()); ++index) {
			vertices.set(index, source[static_cast<size_t>(index)]);
		}
		godot::Array arrays;
		arrays.resize(godot::Mesh::ARRAY_MAX);
		arrays[godot::Mesh::ARRAY_VERTEX] = vertices;
		mesh->add_surface_from_arrays(godot::Mesh::PRIMITIVE_TRIANGLES, arrays);
		mesh->surface_set_name(mesh->get_surface_count() - 1, surface_name(surface));
	}
	return mesh;
}
