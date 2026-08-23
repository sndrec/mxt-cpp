#include "render/native_stamp_mesh_builder.h"

#include "godot_cpp/classes/array_mesh.hpp"
#include "godot_cpp/classes/image.hpp"
#include "godot_cpp/classes/image_texture.hpp"
#include "godot_cpp/classes/mesh.hpp"
#include "godot_cpp/classes/object.hpp"
#include "godot_cpp/classes/time.hpp"
#include "godot_cpp/core/class_db.hpp"
#include "godot_cpp/variant/packed_byte_array.hpp"
#include "godot_cpp/variant/packed_color_array.hpp"
#include "godot_cpp/variant/packed_float32_array.hpp"
#include "godot_cpp/variant/packed_int32_array.hpp"
#include "godot_cpp/variant/packed_vector2_array.hpp"
#include "godot_cpp/variant/packed_vector3_array.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

using namespace godot;

namespace {

constexpr float SURFACE_OFFSET = 0.006f;
constexpr float EPSILON = 0.00001f;
constexpr int OCCLUSION_MAP_SIZE = 128;
constexpr float OCCLUSION_DEPTH_EPSILON = 0.025f;
constexpr float OCCLUSION_DEPTH_EMPTY = -100000000.0f;
constexpr int OCCLUSION_ATLAS_COLUMNS = 8;
constexpr int OCCLUSION_ATLAS_ROWS = 4;

static Vector3 basis_xform(const Basis &basis, const Vector3 &value)
{
	return basis.get_column(0) * value.x + basis.get_column(1) * value.y + basis.get_column(2) * value.z;
}

static Vector3 transform_xform(const Transform3D &transform, const Vector3 &value)
{
	return basis_xform(transform.basis, value) + transform.origin;
}

static Vector3 normalized_or(const Vector3 &value, const Vector3 &fallback)
{
	const real_t len_sq = value.length_squared();
	if (!(len_sq > EPSILON)) {
		return fallback;
	}
	return value / std::sqrt(static_cast<float>(len_sq));
}

static float inverse_lerp_f(float from, float to, float value)
{
	const float denom = to - from;
	if (std::abs(denom) <= EPSILON) {
		return 0.0f;
	}
	return (value - from) / denom;
}

static float clamp_f(float value, float min_value, float max_value)
{
	return std::min(std::max(value, min_value), max_value);
}

static Vector2 projector_uv(const Vector3 &point, const Vector3 &clip_min, const Vector3 &clip_max)
{
	return Vector2(
			inverse_lerp_f(clip_min.x, clip_max.x, point.x),
			inverse_lerp_f(clip_max.y, clip_min.y, point.y));
}

static Vector2 projector_pixel(const Vector3 &point, const Vector3 &clip_min, const Vector3 &clip_max)
{
	return projector_uv(point, clip_min, clip_max) * static_cast<float>(OCCLUSION_MAP_SIZE - 1);
}

static float axis_value(const Vector3 &point, int axis)
{
	if (axis == 0) {
		return point.x;
	}
	if (axis == 1) {
		return point.y;
	}
	return point.z;
}

struct StampSpec {
	int layer = 0;
	Transform3D projector;
	Rect2 atlas_rect;
	float source_flag = 0.0f;
	bool atlas_rotated = false;
	bool flip_horizontal = false;
	bool flip_vertical = false;
	bool mirror_local_x = false;
	Vector2 size = Vector2(1.0f, 1.0f);
	float projection_depth = 0.25f;
	Color colour = Color(1.0f, 1.0f, 1.0f, 1.0f);
	float opacity = 1.0f;
	bool enabled = true;
};

struct ClipVertex {
	Vector3 projector_pos;
	Vector3 car_pos;
	Vector3 body_pos;
	Vector3 normal;
	Vector3 body_normal;
	Vector2 body_uv;
};

static bool parse_stamp_spec(Object *stamp, Object *catalog, StampSpec &out)
{
	if (stamp == nullptr || catalog == nullptr) {
		return false;
	}
	const String source = stamp->get("source");
	const bool is_custom = source == "custom";
	const String stamp_id = stamp->get("stamp_id");
	const Variant atlas_rect_var = catalog->call("get_stamp_atlas_rect", stamp);
	if (atlas_rect_var.get_type() != Variant::RECT2) {
		return false;
	}
	out.layer = static_cast<int>(stamp->get("layer"));
	const Basis basis = stamp->get("local_basis");
	const Vector3 origin = stamp->get("local_origin");
	out.projector = Transform3D(basis, origin);
	out.atlas_rect = atlas_rect_var;
	out.source_flag = is_custom ? 1.0f : 0.0f;
	out.atlas_rotated = static_cast<bool>(stamp->get("custom_rect_rotated"));
	out.flip_horizontal = static_cast<bool>(stamp->get("flip_horizontal"));
	out.flip_vertical = static_cast<bool>(stamp->get("flip_vertical"));
	out.mirror_local_x = static_cast<bool>(stamp->get("mirror_local_x"));
	out.size = stamp->get("size");
	out.projection_depth = std::max(EPSILON, static_cast<float>(stamp->get("projection_depth")));
	out.colour = stamp->get("colour");
	out.opacity = clamp_f(static_cast<float>(stamp->get("opacity")), 0.0f, 1.0f);
	out.enabled = static_cast<bool>(stamp->get("enabled"));
	return out.enabled &&
			out.opacity > 0.0f &&
			(is_custom || !stamp_id.is_empty()) &&
			std::abs(static_cast<float>(out.projector.basis.determinant())) > EPSILON &&
			out.atlas_rect.size.x > 0.0f &&
			out.atlas_rect.size.y > 0.0f;
}

static Dictionary stamp_spec_to_dictionary(const StampSpec &stamp)
{
	Dictionary out;
	out["layer"] = stamp.layer;
	out["projector"] = stamp.projector;
	out["atlas_rect"] = stamp.atlas_rect;
	out["source_flag"] = stamp.source_flag;
	out["atlas_rotated"] = stamp.atlas_rotated;
	out["flip_horizontal"] = stamp.flip_horizontal;
	out["flip_vertical"] = stamp.flip_vertical;
	out["mirror_local_x"] = stamp.mirror_local_x;
	out["size"] = stamp.size;
	out["projection_depth"] = stamp.projection_depth;
	out["colour"] = stamp.colour;
	out["opacity"] = stamp.opacity;
	return out;
}

static StampSpec stamp_spec_from_dictionary(const Dictionary &source)
{
	StampSpec out;
	out.layer = static_cast<int>(source.get("layer", 0));
	out.projector = source.get("projector", Transform3D());
	out.atlas_rect = source.get("atlas_rect", Rect2());
	out.source_flag = static_cast<float>(source.get("source_flag", 0.0));
	out.atlas_rotated = static_cast<bool>(source.get("atlas_rotated", false));
	out.flip_horizontal = static_cast<bool>(source.get("flip_horizontal", false));
	out.flip_vertical = static_cast<bool>(source.get("flip_vertical", false));
	out.mirror_local_x = static_cast<bool>(source.get("mirror_local_x", false));
	out.size = source.get("size", Vector2(1.0f, 1.0f));
	out.projection_depth = static_cast<float>(source.get("projection_depth", 0.25));
	out.colour = source.get("colour", Color(1.0f, 1.0f, 1.0f, 1.0f));
	out.opacity = static_cast<float>(source.get("opacity", 1.0));
	out.enabled = true;
	return out;
}

static PackedVector3Array surface_vertices(const Array &arrays)
{
	if (arrays.size() <= Mesh::ARRAY_VERTEX) {
		return PackedVector3Array();
	}
	const Variant value = arrays[Mesh::ARRAY_VERTEX];
	if (value.get_type() != Variant::PACKED_VECTOR3_ARRAY) {
		return PackedVector3Array();
	}
	return value;
}

static PackedVector3Array surface_normals(const Array &arrays)
{
	if (arrays.size() <= Mesh::ARRAY_NORMAL) {
		return PackedVector3Array();
	}
	const Variant value = arrays[Mesh::ARRAY_NORMAL];
	if (value.get_type() != Variant::PACKED_VECTOR3_ARRAY) {
		return PackedVector3Array();
	}
	return value;
}

static PackedVector2Array surface_uvs(const Array &arrays)
{
	if (arrays.size() <= Mesh::ARRAY_TEX_UV) {
		return PackedVector2Array();
	}
	const Variant value = arrays[Mesh::ARRAY_TEX_UV];
	if (value.get_type() != Variant::PACKED_VECTOR2_ARRAY) {
		return PackedVector2Array();
	}
	return value;
}

static PackedInt32Array surface_indices(const Array &arrays)
{
	if (arrays.size() <= Mesh::ARRAY_INDEX) {
		return PackedInt32Array();
	}
	const Variant value = arrays[Mesh::ARRAY_INDEX];
	if (value.get_type() != Variant::PACKED_INT32_ARRAY) {
		return PackedInt32Array();
	}
	return value;
}

static Vector3 normal_at(const Transform3D &body_to_car, const PackedVector3Array &normals, int index, const Vector3 &fallback)
{
	if (index < 0 || index >= normals.size()) {
		return fallback;
	}
	const Vector3 normal = basis_xform(body_to_car.basis, normals[index]);
	return normalized_or(normal, fallback);
}

static Vector3 body_normal_at(const PackedVector3Array &normals, int index, const Vector3 &fallback)
{
	if (index < 0 || index >= normals.size()) {
		return fallback;
	}
	return normalized_or(normals[index], fallback);
}

static Vector2 uv_at(const PackedVector2Array &uvs, int index)
{
	if (index < 0 || index >= uvs.size()) {
		return Vector2();
	}
	return uvs[index];
}

static bool point_inside_axis(const Vector3 &point, int axis, float plane, bool keep_greater)
{
	const float value = axis_value(point, axis);
	return keep_greater ? value >= plane - EPSILON : value <= plane + EPSILON;
}

static ClipVertex interpolate_clip_vertex(const ClipVertex &a, const ClipVertex &b, int axis, float plane)
{
	const float denom = axis_value(b.projector_pos, axis) - axis_value(a.projector_pos, axis);
	float t = 0.0f;
	if (std::abs(denom) > EPSILON) {
		t = (plane - axis_value(a.projector_pos, axis)) / denom;
	}
	t = clamp_f(t, 0.0f, 1.0f);

	ClipVertex out;
	out.projector_pos = a.projector_pos.lerp(b.projector_pos, t);
	out.car_pos = a.car_pos.lerp(b.car_pos, t);
	out.body_pos = a.body_pos.lerp(b.body_pos, t);
	out.normal = normalized_or(a.normal.lerp(b.normal, t), a.normal);
	out.body_normal = normalized_or(a.body_normal.lerp(b.body_normal, t), a.body_normal);
	out.body_uv = a.body_uv.lerp(b.body_uv, t);
	return out;
}

static void clip_polygon_axis(std::vector<ClipVertex> &polygon, int axis, float plane, bool keep_greater)
{
	if (polygon.empty()) {
		return;
	}
	std::vector<ClipVertex> out;
	out.reserve(polygon.size() + 2);
	ClipVertex previous = polygon.back();
	bool previous_inside = point_inside_axis(previous.projector_pos, axis, plane, keep_greater);
	for (const ClipVertex &current : polygon) {
		const bool current_inside = point_inside_axis(current.projector_pos, axis, plane, keep_greater);
		if (current_inside != previous_inside) {
			out.push_back(interpolate_clip_vertex(previous, current, axis, plane));
		}
		if (current_inside) {
			out.push_back(current);
		}
		previous = current;
		previous_inside = current_inside;
	}
	polygon.swap(out);
}

static std::vector<ClipVertex> clipped_triangle_polygon(
		const Transform3D &body_to_car,
		const Transform3D &car_to_projector,
		const Vector3 &clip_min,
		const Vector3 &clip_max,
		const PackedVector3Array &vertices,
		const PackedVector3Array &normals,
		const PackedVector2Array &body_uvs,
		int i0,
		int i1,
		int i2)
{
	if (i0 < 0 || i1 < 0 || i2 < 0 || i0 >= vertices.size() || i1 >= vertices.size() || i2 >= vertices.size()) {
		return {};
	}
	const Vector3 p0 = transform_xform(body_to_car, vertices[i0]);
	const Vector3 p1 = transform_xform(body_to_car, vertices[i1]);
	const Vector3 p2 = transform_xform(body_to_car, vertices[i2]);
	const Vector3 q0 = transform_xform(car_to_projector, p0);
	const Vector3 q1 = transform_xform(car_to_projector, p1);
	const Vector3 q2 = transform_xform(car_to_projector, p2);
	if ((q0.x < clip_min.x && q1.x < clip_min.x && q2.x < clip_min.x) ||
			(q0.x > clip_max.x && q1.x > clip_max.x && q2.x > clip_max.x) ||
			(q0.y < clip_min.y && q1.y < clip_min.y && q2.y < clip_min.y) ||
			(q0.y > clip_max.y && q1.y > clip_max.y && q2.y > clip_max.y) ||
			(q0.z < clip_min.z && q1.z < clip_min.z && q2.z < clip_min.z) ||
			(q0.z > clip_max.z && q1.z > clip_max.z && q2.z > clip_max.z)) {
		return {};
	}
	Vector3 face_normal = (p1 - p0).cross(p2 - p0);
	if (face_normal.length_squared() <= EPSILON) {
		return {};
	}
	face_normal.normalize();
	Vector3 body_face_normal = (vertices[i1] - vertices[i0]).cross(vertices[i2] - vertices[i0]);
	body_face_normal = normalized_or(body_face_normal, Vector3(0.0f, 1.0f, 0.0f));

	std::vector<ClipVertex> polygon;
	polygon.reserve(8);
	polygon.push_back({ q0, p0, vertices[i0], normal_at(body_to_car, normals, i0, face_normal), body_normal_at(normals, i0, body_face_normal), uv_at(body_uvs, i0) });
	polygon.push_back({ q1, p1, vertices[i1], normal_at(body_to_car, normals, i1, face_normal), body_normal_at(normals, i1, body_face_normal), uv_at(body_uvs, i1) });
	polygon.push_back({ q2, p2, vertices[i2], normal_at(body_to_car, normals, i2, face_normal), body_normal_at(normals, i2, body_face_normal), uv_at(body_uvs, i2) });

	clip_polygon_axis(polygon, 0, clip_min.x, true);
	clip_polygon_axis(polygon, 0, clip_max.x, false);
	clip_polygon_axis(polygon, 1, clip_min.y, true);
	clip_polygon_axis(polygon, 1, clip_max.y, false);
	clip_polygon_axis(polygon, 2, clip_min.z, true);
	clip_polygon_axis(polygon, 2, clip_max.z, false);
	return polygon;
}

static float barycentric_denominator(const Vector2 &a, const Vector2 &b, const Vector2 &c)
{
	return (b.y - c.y) * (a.x - c.x) + (c.x - b.x) * (a.y - c.y);
}

static Vector3 barycentric(const Vector2 &point, const Vector2 &a, const Vector2 &b, const Vector2 &c, float denom)
{
	const float w0 = ((b.y - c.y) * (point.x - c.x) + (c.x - b.x) * (point.y - c.y)) / denom;
	const float w1 = ((c.y - a.y) * (point.x - c.x) + (a.x - c.x) * (point.y - c.y)) / denom;
	return Vector3(w0, w1, 1.0f - w0 - w1);
}

static void raster_depth_triangle(const Vector3 &a, const Vector3 &b, const Vector3 &c, const Vector3 &clip_min, const Vector3 &clip_max, std::vector<float> &depth_map)
{
	const Vector2 a_pixel = projector_pixel(a, clip_min, clip_max);
	const Vector2 b_pixel = projector_pixel(b, clip_min, clip_max);
	const Vector2 c_pixel = projector_pixel(c, clip_min, clip_max);
	const int min_x = std::max(0, static_cast<int>(std::floor(std::min(a_pixel.x, std::min(b_pixel.x, c_pixel.x)))));
	const int max_x = std::min(OCCLUSION_MAP_SIZE - 1, static_cast<int>(std::ceil(std::max(a_pixel.x, std::max(b_pixel.x, c_pixel.x)))));
	const int min_y = std::max(0, static_cast<int>(std::floor(std::min(a_pixel.y, std::min(b_pixel.y, c_pixel.y)))));
	const int max_y = std::min(OCCLUSION_MAP_SIZE - 1, static_cast<int>(std::ceil(std::max(a_pixel.y, std::max(b_pixel.y, c_pixel.y)))));
	if (min_x > max_x || min_y > max_y) {
		return;
	}
	const float denom = barycentric_denominator(a_pixel, b_pixel, c_pixel);
	if (std::abs(denom) <= EPSILON) {
		return;
	}
	for (int y = min_y; y <= max_y; ++y) {
		for (int x = min_x; x <= max_x; ++x) {
			const Vector2 sample(static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f);
			const Vector3 bary = barycentric(sample, a_pixel, b_pixel, c_pixel, denom);
			if (bary.x < -EPSILON || bary.y < -EPSILON || bary.z < -EPSILON) {
				continue;
			}
			const float depth = a.z * bary.x + b.z * bary.y + c.z * bary.z;
			const int depth_index = y * OCCLUSION_MAP_SIZE + x;
			if (depth > depth_map[depth_index]) {
				depth_map[depth_index] = depth;
			}
		}
	}
}

static void raster_depth_polygon(const std::vector<ClipVertex> &polygon, const Vector3 &clip_min, const Vector3 &clip_max, std::vector<float> &depth_map)
{
	if (polygon.size() < 3) {
		return;
	}
	for (int i = 1; i < static_cast<int>(polygon.size()) - 1; ++i) {
		raster_depth_triangle(polygon[0].projector_pos, polygon[i].projector_pos, polygon[i + 1].projector_pos, clip_min, clip_max, depth_map);
	}
}

static Vector2i mask_tile_origin(int mask_slot)
{
	return Vector2i(mask_slot % OCCLUSION_ATLAS_COLUMNS, mask_slot / OCCLUSION_ATLAS_COLUMNS) * OCCLUSION_MAP_SIZE;
}

static Rect2 mask_rect_for_slot(int mask_slot)
{
	const Vector2 atlas_size(static_cast<float>(OCCLUSION_ATLAS_COLUMNS * OCCLUSION_MAP_SIZE), static_cast<float>(OCCLUSION_ATLAS_ROWS * OCCLUSION_MAP_SIZE));
	const Vector2i tile_origin_i = mask_tile_origin(mask_slot);
	const Vector2 tile_origin(static_cast<float>(tile_origin_i.x), static_cast<float>(tile_origin_i.y));
	return Rect2(tile_origin / atlas_size, Vector2(static_cast<float>(OCCLUSION_MAP_SIZE), static_cast<float>(OCCLUSION_MAP_SIZE)) / atlas_size);
}

static Vector2 mask_uv_for_projector_uv(const Rect2 &mask_rect, const Vector2 &uv)
{
	const Vector2 atlas_size(static_cast<float>(OCCLUSION_ATLAS_COLUMNS * OCCLUSION_MAP_SIZE), static_cast<float>(OCCLUSION_ATLAS_ROWS * OCCLUSION_MAP_SIZE));
	const Vector2 tile_pixel = uv * static_cast<float>(OCCLUSION_MAP_SIZE - 1) + Vector2(0.5f, 0.5f);
	return mask_rect.position + tile_pixel / atlas_size;
}

static void write_depth_map_to_mask_pixels(const std::vector<float> &depth_map, const Vector3 &clip_min, const Vector3 &clip_max, int mask_slot, float *mask_pixels)
{
	const Vector2i tile_origin = mask_tile_origin(mask_slot);
	for (int y = 0; y < OCCLUSION_MAP_SIZE; ++y) {
		for (int x = 0; x < OCCLUSION_MAP_SIZE; ++x) {
			const float depth = depth_map[y * OCCLUSION_MAP_SIZE + x];
			float encoded_depth = 0.0f;
			float valid = 0.0f;
			if (depth > OCCLUSION_DEPTH_EMPTY * 0.5f) {
				encoded_depth = clamp_f(inverse_lerp_f(clip_min.z, clip_max.z, depth), 0.0f, 1.0f);
				valid = 1.0f;
			}
			const int atlas_x = tile_origin.x + x;
			const int atlas_y = tile_origin.y + y;
			const size_t pixel_offset = static_cast<size_t>((atlas_y * OCCLUSION_ATLAS_COLUMNS * OCCLUSION_MAP_SIZE + atlas_x) * 2);
			mask_pixels[pixel_offset] = encoded_depth;
			mask_pixels[pixel_offset + 1] = valid;
		}
	}
}

static Vector2 atlas_uv_for_projector_uv(const Rect2 &atlas_rect, Vector2 projector_uv, bool atlas_rotated, bool flip_horizontal, bool flip_vertical)
{
	if (flip_horizontal) {
		projector_uv.x = 1.0f - projector_uv.x;
	}
	if (flip_vertical) {
		projector_uv.y = 1.0f - projector_uv.y;
	}
	if (atlas_rotated) {
		return atlas_rect.position + Vector2(projector_uv.y, 1.0f - projector_uv.x) * atlas_rect.size;
	}
	return atlas_rect.position + projector_uv * atlas_rect.size;
}

static void emit_vertex(
		const ClipVertex &vertex,
		const Transform3D &car_to_body,
		const Vector3 &clip_min,
		const Vector3 &clip_max,
		const Rect2 &atlas_rect,
		float source_flag,
		bool atlas_rotated,
		bool flip_horizontal,
		bool flip_vertical,
		const Rect2 &mask_rect,
		const Color &colour,
		float depth_epsilon_normalized,
		PackedVector3Array &out_vertices,
		PackedVector3Array &out_normals,
		PackedVector2Array &out_body_uvs,
		PackedVector2Array &out_stamp_uvs,
		PackedColorArray &out_colours,
		PackedFloat32Array &out_mask_data,
		PackedFloat32Array &out_source_data)
{
	const Vector2 proj_uv = projector_uv(vertex.projector_pos, clip_min, clip_max);
	const Vector2 atlas_uv = atlas_uv_for_projector_uv(atlas_rect, proj_uv, atlas_rotated, flip_horizontal, flip_vertical);
	const Vector2 mask_uv = mask_uv_for_projector_uv(mask_rect, proj_uv);
	const float projector_depth = clamp_f(inverse_lerp_f(clip_min.z, clip_max.z, vertex.projector_pos.z), 0.0f, 1.0f);
	out_vertices.append(transform_xform(car_to_body, vertex.car_pos + vertex.normal * SURFACE_OFFSET));
	out_normals.append(vertex.body_normal);
	out_body_uvs.append(vertex.body_uv);
	out_stamp_uvs.append(atlas_uv);
	out_colours.append(colour);
	out_mask_data.append(mask_uv.x);
	out_mask_data.append(mask_uv.y);
	out_mask_data.append(projector_depth);
	out_mask_data.append(depth_epsilon_normalized);
	out_source_data.append(source_flag);
	out_source_data.append(0.0f);
	out_source_data.append(0.0f);
	out_source_data.append(0.0f);
}

static void append_clipped_polygon(
		const std::vector<ClipVertex> &polygon,
		const Transform3D &car_to_body,
		const Vector3 &clip_min,
		const Vector3 &clip_max,
		const Rect2 &atlas_rect,
		float source_flag,
		bool atlas_rotated,
		bool flip_horizontal,
		bool flip_vertical,
		const Rect2 &mask_rect,
		const Color &colour,
		float depth_epsilon_normalized,
		PackedVector3Array &out_vertices,
		PackedVector3Array &out_normals,
		PackedVector2Array &out_body_uvs,
		PackedVector2Array &out_stamp_uvs,
		PackedColorArray &out_colours,
		PackedFloat32Array &out_mask_data,
		PackedFloat32Array &out_source_data)
{
	if (polygon.size() < 3) {
		return;
	}
	for (int i = 1; i < static_cast<int>(polygon.size()) - 1; ++i) {
		emit_vertex(polygon[0], car_to_body, clip_min, clip_max, atlas_rect, source_flag, atlas_rotated, flip_horizontal, flip_vertical, mask_rect, colour, depth_epsilon_normalized, out_vertices, out_normals, out_body_uvs, out_stamp_uvs, out_colours, out_mask_data, out_source_data);
		emit_vertex(polygon[i], car_to_body, clip_min, clip_max, atlas_rect, source_flag, atlas_rotated, flip_horizontal, flip_vertical, mask_rect, colour, depth_epsilon_normalized, out_vertices, out_normals, out_body_uvs, out_stamp_uvs, out_colours, out_mask_data, out_source_data);
		emit_vertex(polygon[i + 1], car_to_body, clip_min, clip_max, atlas_rect, source_flag, atlas_rotated, flip_horizontal, flip_vertical, mask_rect, colour, depth_epsilon_normalized, out_vertices, out_normals, out_body_uvs, out_stamp_uvs, out_colours, out_mask_data, out_source_data);
	}
}

static void append_stamp_projection(
		const Array &surfaces,
		const Transform3D &body_to_car,
		const Transform3D &car_to_body,
		const StampSpec &stamp,
		int mask_slot,
		float *mask_pixels,
		bool build_visibility_mask,
		const Transform3D &projector,
		PackedVector3Array &out_vertices,
		PackedVector3Array &out_normals,
		PackedVector2Array &out_body_uvs,
		PackedVector2Array &out_stamp_uvs,
		PackedColorArray &out_colours,
		PackedFloat32Array &out_mask_data,
		PackedFloat32Array &out_source_data,
		uint64_t &visibility_mask_usec,
		uint64_t &geometry_projection_usec)
{
	if (std::abs(static_cast<float>(projector.basis.determinant())) <= EPSILON) {
		return;
	}
	const Transform3D car_to_projector = projector.affine_inverse();
	const Vector2 half_size(std::max(static_cast<float>(stamp.size.x), EPSILON) * 0.5f, std::max(static_cast<float>(stamp.size.y), EPSILON) * 0.5f);
	const float half_depth = std::max(stamp.projection_depth, EPSILON) * 0.5f;
	const Vector3 clip_min(-half_size.x, -half_size.y, -half_depth);
	const Vector3 clip_max(half_size.x, half_size.y, half_depth);
	const Color stamp_colour(stamp.colour.r, stamp.colour.g, stamp.colour.b, stamp.colour.a * stamp.opacity);
	const float depth_epsilon = std::max(OCCLUSION_DEPTH_EPSILON, half_depth * 0.02f);
	const float depth_range = std::max(static_cast<float>(clip_max.z - clip_min.z), EPSILON);
	const float depth_epsilon_normalized = clamp_f(depth_epsilon / depth_range, 0.0f, 1.0f);
	const Rect2 mask_rect = mask_pixels != nullptr ? mask_rect_for_slot(mask_slot) : Rect2(0.0f, 0.0f, 1.0f, 1.0f);

	std::vector<float> depth_map;
	if (build_visibility_mask && mask_pixels != nullptr) {
		depth_map.assign(OCCLUSION_MAP_SIZE * OCCLUSION_MAP_SIZE, OCCLUSION_DEPTH_EMPTY);
	}
	const uint64_t phase_start_usec = Time::get_singleton()->get_ticks_usec();
	for (int surface_index = 0; surface_index < surfaces.size(); ++surface_index) {
		if (surfaces[surface_index].get_type() != Variant::ARRAY) continue;
		const Array arrays = surfaces[surface_index];
		const PackedVector3Array vertices = surface_vertices(arrays);
		if (vertices.is_empty()) {
			continue;
		}
		const PackedVector3Array normals = surface_normals(arrays);
		const PackedVector2Array body_uvs = surface_uvs(arrays);
		const PackedInt32Array indices = surface_indices(arrays);
		if (indices.is_empty()) {
			const int tri_count = vertices.size() / 3;
			for (int tri = 0; tri < tri_count; ++tri) {
				const int i0 = tri * 3;
				const std::vector<ClipVertex> polygon = clipped_triangle_polygon(body_to_car, car_to_projector, clip_min, clip_max, vertices, normals, body_uvs, i0, i0 + 1, i0 + 2);
				if (!depth_map.empty()) raster_depth_polygon(polygon, clip_min, clip_max, depth_map);
				append_clipped_polygon(polygon, car_to_body, clip_min, clip_max, stamp.atlas_rect, stamp.source_flag, stamp.atlas_rotated, stamp.flip_horizontal, stamp.flip_vertical, mask_rect, stamp_colour, depth_epsilon_normalized, out_vertices, out_normals, out_body_uvs, out_stamp_uvs, out_colours, out_mask_data, out_source_data);
			}
		} else {
			const int tri_count = indices.size() / 3;
			for (int tri = 0; tri < tri_count; ++tri) {
				const std::vector<ClipVertex> polygon = clipped_triangle_polygon(body_to_car, car_to_projector, clip_min, clip_max, vertices, normals, body_uvs, indices[tri * 3], indices[tri * 3 + 1], indices[tri * 3 + 2]);
				if (!depth_map.empty()) raster_depth_polygon(polygon, clip_min, clip_max, depth_map);
				append_clipped_polygon(polygon, car_to_body, clip_min, clip_max, stamp.atlas_rect, stamp.source_flag, stamp.atlas_rotated, stamp.flip_horizontal, stamp.flip_vertical, mask_rect, stamp_colour, depth_epsilon_normalized, out_vertices, out_normals, out_body_uvs, out_stamp_uvs, out_colours, out_mask_data, out_source_data);
			}
		}
	}
	geometry_projection_usec += Time::get_singleton()->get_ticks_usec() - phase_start_usec;
	if (!depth_map.empty()) {
		const uint64_t mask_write_start_usec = Time::get_singleton()->get_ticks_usec();
		write_depth_map_to_mask_pixels(depth_map, clip_min, clip_max, mask_slot, mask_pixels);
		visibility_mask_usec += Time::get_singleton()->get_ticks_usec() - mask_write_start_usec;
	}
}

static Dictionary empty_build_result()
{
	Dictionary out;
	Ref<ArrayMesh> mesh;
	mesh.instantiate();
	out["mesh"] = mesh;
	out["visibility_mask"] = Variant();
	out["stamp_vertex_ranges"] = Dictionary();
	return out;
}

} // namespace

void NativeStampMeshBuilder::_bind_methods()
{
	ClassDB::bind_method(D_METHOD("snapshot_build_inputs", "body_mesh", "body_to_car", "livery", "catalog", "build_visibility_masks", "visibility_mask_skip_layer"), &NativeStampMeshBuilder::snapshot_build_inputs, DEFVAL(true), DEFVAL(-1));
	ClassDB::bind_method(D_METHOD("prepare_snapshot", "snapshot"), &NativeStampMeshBuilder::prepare_snapshot);
	ClassDB::bind_method(D_METHOD("install_prepared", "prepared"), &NativeStampMeshBuilder::install_prepared);
	ClassDB::bind_method(D_METHOD("build_for_body_mesh_with_masks", "body_mesh", "body_to_car", "livery", "catalog", "build_visibility_masks", "visibility_mask_skip_layer"), &NativeStampMeshBuilder::build_for_body_mesh_with_masks, DEFVAL(true), DEFVAL(-1));
}

Dictionary NativeStampMeshBuilder::snapshot_build_inputs(
		MeshInstance3D *p_body_mesh,
		const Transform3D &p_body_to_car,
		Object *p_livery,
		Object *p_catalog,
		bool p_build_visibility_masks,
		int p_visibility_mask_skip_layer)
{
	const uint64_t snapshot_start_usec = Time::get_singleton()->get_ticks_usec();
	if (p_body_mesh == nullptr || p_livery == nullptr || p_catalog == nullptr) {
		return Dictionary();
	}
	Ref<Mesh> mesh = p_body_mesh->get_mesh();
	if (mesh.is_null()) {
		return Dictionary();
	}
	const Variant sorted_stamps_var = p_livery->call("get_sorted_stamps");
	if (sorted_stamps_var.get_type() != Variant::ARRAY) {
		return Dictionary();
	}
	const Array sorted_stamps = sorted_stamps_var;
	if (sorted_stamps.is_empty()) {
		return Dictionary();
	}
	Array surfaces;
	surfaces.resize(mesh->get_surface_count());
	for (int i = 0; i < mesh->get_surface_count(); ++i) surfaces[i] = mesh->surface_get_arrays(i);
	Array stamps;
	uint64_t stamp_parse_usec = 0;
	for (int i = 0; i < sorted_stamps.size(); ++i) {
		Object *stamp_object = Object::cast_to<Object>(sorted_stamps[i]);
		if (stamp_object == nullptr) continue;
		StampSpec stamp;
		const uint64_t parse_start_usec = Time::get_singleton()->get_ticks_usec();
		const bool valid = parse_stamp_spec(stamp_object, p_catalog, stamp);
		stamp_parse_usec += Time::get_singleton()->get_ticks_usec() - parse_start_usec;
		if (valid) stamps.push_back(stamp_spec_to_dictionary(stamp));
	}
	if (stamps.is_empty()) return Dictionary();
	Dictionary snapshot;
	snapshot["surfaces"] = surfaces;
	snapshot["stamps"] = stamps;
	snapshot["body_to_car"] = p_body_to_car;
	snapshot["build_visibility_masks"] = p_build_visibility_masks;
	snapshot["visibility_mask_skip_layer"] = p_visibility_mask_skip_layer;
	snapshot["snapshot_usec"] = static_cast<int64_t>(Time::get_singleton()->get_ticks_usec() - snapshot_start_usec);
	snapshot["stamp_parse_usec"] = static_cast<int64_t>(stamp_parse_usec);
	return snapshot;
}

Dictionary NativeStampMeshBuilder::prepare_snapshot(const Dictionary &p_snapshot)
{
	const uint64_t total_start_usec = Time::get_singleton()->get_ticks_usec();
	const Array surfaces = p_snapshot.get("surfaces", Array());
	const Array stamp_values = p_snapshot.get("stamps", Array());
	if (surfaces.is_empty() || stamp_values.is_empty()) return Dictionary();
	const Transform3D body_to_car = p_snapshot.get("body_to_car", Transform3D());
	const bool build_visibility_masks = static_cast<bool>(p_snapshot.get("build_visibility_masks", true));
	const int visibility_mask_skip_layer = static_cast<int>(p_snapshot.get("visibility_mask_skip_layer", -1));

	PackedVector3Array out_vertices;
	PackedVector3Array out_normals;
	PackedVector2Array out_body_uvs;
	PackedVector2Array out_stamp_uvs;
	PackedColorArray out_colours;
	PackedFloat32Array out_mask_data;
	PackedFloat32Array out_source_data;
	Dictionary stamp_vertex_ranges;

	PackedByteArray mask_bytes;
	float *mask_pixels = nullptr;
	uint64_t mask_image_setup_usec = 0;
	if (build_visibility_masks) {
		const uint64_t phase_start_usec = Time::get_singleton()->get_ticks_usec();
		mask_bytes.resize(static_cast<int64_t>(OCCLUSION_ATLAS_COLUMNS * OCCLUSION_MAP_SIZE * OCCLUSION_ATLAS_ROWS * OCCLUSION_MAP_SIZE * 2 * sizeof(float)));
		std::fill(mask_bytes.ptrw(), mask_bytes.ptrw() + mask_bytes.size(), static_cast<uint8_t>(0));
		mask_pixels = reinterpret_cast<float *>(mask_bytes.ptrw());
		mask_image_setup_usec = Time::get_singleton()->get_ticks_usec() - phase_start_usec;
	}

	const Transform3D car_to_body = body_to_car.affine_inverse();
	uint64_t visibility_mask_usec = 0;
	uint64_t geometry_projection_usec = 0;
	int mask_slot = 0;
	const int max_mask_slots = OCCLUSION_ATLAS_COLUMNS * OCCLUSION_ATLAS_ROWS;
	for (int i = 0; i < stamp_values.size(); ++i) {
		if (stamp_values[i].get_type() != Variant::DICTIONARY) continue;
		const StampSpec stamp = stamp_spec_from_dictionary(stamp_values[i]);
		const int stamp_mask_slots = stamp.mirror_local_x ? 2 : 1;
		if (mask_slot + stamp_mask_slots > max_mask_slots) {
			break;
		}
		const int vertex_start = out_vertices.size();
		const bool build_visibility_mask = build_visibility_masks && stamp.layer != visibility_mask_skip_layer;
		append_stamp_projection(surfaces, body_to_car, car_to_body, stamp, mask_slot, mask_pixels, build_visibility_mask, stamp.projector, out_vertices, out_normals, out_body_uvs, out_stamp_uvs, out_colours, out_mask_data, out_source_data, visibility_mask_usec, geometry_projection_usec);
		if (stamp.mirror_local_x) {
			const Basis mirror_basis(Vector3(-1.0f, 0.0f, 0.0f), Vector3(0.0f, 1.0f, 0.0f), Vector3(0.0f, 0.0f, 1.0f));
			const Transform3D mirrored_projector = Transform3D(mirror_basis, Vector3()) * stamp.projector;
			append_stamp_projection(surfaces, body_to_car, car_to_body, stamp, mask_slot + 1, mask_pixels, build_visibility_mask, mirrored_projector, out_vertices, out_normals, out_body_uvs, out_stamp_uvs, out_colours, out_mask_data, out_source_data, visibility_mask_usec, geometry_projection_usec);
		}
		const int vertex_count = out_vertices.size() - vertex_start;
		if (vertex_count > 0) {
			Dictionary range;
			range["start"] = vertex_start;
			range["count"] = vertex_count;
			stamp_vertex_ranges[stamp.layer] = range;
		}
		mask_slot += stamp_mask_slots;
		if (mask_slot >= max_mask_slots) {
			break;
		}
	}

	Array arrays;
	arrays.resize(Mesh::ARRAY_MAX);
	arrays[Mesh::ARRAY_VERTEX] = out_vertices;
	arrays[Mesh::ARRAY_NORMAL] = out_normals;
	arrays[Mesh::ARRAY_TEX_UV] = out_body_uvs;
	arrays[Mesh::ARRAY_TEX_UV2] = out_stamp_uvs;
	arrays[Mesh::ARRAY_COLOR] = out_colours;
	arrays[Mesh::ARRAY_CUSTOM0] = out_mask_data;
	arrays[Mesh::ARRAY_CUSTOM1] = out_source_data;
	Dictionary result;
	result["arrays"] = arrays;
	result["mask_bytes"] = mask_bytes;
	result["stamp_vertex_ranges"] = stamp_vertex_ranges;
	Dictionary profile;
	profile["total_usec"] = static_cast<int64_t>(Time::get_singleton()->get_ticks_usec() - total_start_usec);
	profile["mask_image_setup_usec"] = static_cast<int64_t>(mask_image_setup_usec);
	profile["snapshot_usec"] = static_cast<int64_t>(p_snapshot.get("snapshot_usec", 0));
	profile["stamp_parse_usec"] = static_cast<int64_t>(p_snapshot.get("stamp_parse_usec", 0));
	profile["visibility_mask_usec"] = static_cast<int64_t>(visibility_mask_usec);
	profile["geometry_projection_usec"] = static_cast<int64_t>(geometry_projection_usec);
	profile["temporary_bytes"] = static_cast<int64_t>(
			out_vertices.size() * sizeof(float) * 3 +
			out_normals.size() * sizeof(float) * 3 +
			out_body_uvs.size() * sizeof(float) * 2 +
			out_stamp_uvs.size() * sizeof(float) * 2 +
			out_colours.size() * sizeof(float) * 4 +
			out_mask_data.size() * sizeof(float) +
			out_source_data.size() * sizeof(float) +
			mask_bytes.size());
	result["profile"] = profile;
	return result;
}

Dictionary NativeStampMeshBuilder::install_prepared(const Dictionary &p_prepared)
{
	Ref<ArrayMesh> out_mesh;
	out_mesh.instantiate();
	Dictionary result;
	Dictionary profile = p_prepared.get("profile", Dictionary());
	const Array arrays = p_prepared.get("arrays", Array());
	if (!arrays.is_empty() && arrays.size() > Mesh::ARRAY_VERTEX && static_cast<PackedVector3Array>(arrays[Mesh::ARRAY_VERTEX]).size() > 0) {
		const int64_t format_flags =
				(static_cast<int64_t>(Mesh::ARRAY_CUSTOM_RGBA_FLOAT) << static_cast<int64_t>(Mesh::ARRAY_FORMAT_CUSTOM0_SHIFT)) |
				(static_cast<int64_t>(Mesh::ARRAY_CUSTOM_RGBA_FLOAT) << static_cast<int64_t>(Mesh::ARRAY_FORMAT_CUSTOM1_SHIFT));
		const uint64_t mesh_upload_start_usec = Time::get_singleton()->get_ticks_usec();
		out_mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays, Array(), Dictionary(), BitField<Mesh::ArrayFormat>(format_flags));
		profile["mesh_upload_usec"] = static_cast<int64_t>(Time::get_singleton()->get_ticks_usec() - mesh_upload_start_usec);
	}
	Ref<Texture2D> visibility_mask;
	const PackedByteArray mask_bytes = p_prepared.get("mask_bytes", PackedByteArray());
	if (!mask_bytes.is_empty() && out_mesh->get_surface_count() > 0) {
		const Ref<Image> mask_image = Image::create_from_data(
				OCCLUSION_ATLAS_COLUMNS * OCCLUSION_MAP_SIZE,
				OCCLUSION_ATLAS_ROWS * OCCLUSION_MAP_SIZE,
				false,
				Image::FORMAT_RGF,
				mask_bytes);
		const uint64_t texture_upload_start_usec = Time::get_singleton()->get_ticks_usec();
		visibility_mask = ImageTexture::create_from_image(mask_image);
		profile["texture_upload_usec"] = static_cast<int64_t>(Time::get_singleton()->get_ticks_usec() - texture_upload_start_usec);
	}
	result["mesh"] = out_mesh;
	result["visibility_mask"] = visibility_mask;
	result["stamp_vertex_ranges"] = p_prepared.get("stamp_vertex_ranges", Dictionary());
	result["profile"] = profile;
	return result;
}

Dictionary NativeStampMeshBuilder::build_for_body_mesh_with_masks(
		MeshInstance3D *p_body_mesh,
		const Transform3D &p_body_to_car,
		Object *p_livery,
		Object *p_catalog,
		bool p_build_visibility_masks,
		int p_visibility_mask_skip_layer)
{
	const Dictionary snapshot = snapshot_build_inputs(p_body_mesh, p_body_to_car, p_livery, p_catalog, p_build_visibility_masks, p_visibility_mask_skip_layer);
	if (snapshot.is_empty()) return empty_build_result();
	const Dictionary prepared = prepare_snapshot(snapshot);
	if (prepared.is_empty()) return empty_build_result();
	return install_prepared(prepared);
}
