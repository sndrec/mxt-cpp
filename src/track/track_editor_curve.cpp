#include "track/track_editor_curve.h"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/basis.hpp>
#include <godot_cpp/variant/quaternion.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <algorithm>
#include <cmath>

namespace godot {

static constexpr float PI_F = 3.14159265358979323846f;

static inline float clampf_local(float p_value, float p_min, float p_max) {
	return p_value < p_min ? p_min : (p_value > p_max ? p_max : p_value);
}

static inline float remapf_local(float p_value, float p_istart, float p_istop, float p_ostart, float p_ostop) {
	const float denom = p_istop - p_istart;
	if (std::fabs(denom) <= 0.000001f) {
		return p_ostart;
	}
	return p_ostart + ((p_value - p_istart) / denom) * (p_ostop - p_ostart);
}

enum TrackEditorMeshMaterial {
	MESH_MATERIAL_TRACK_SURFACE = 0,
	MESH_MATERIAL_TRACK_RAIL = 1,
	MESH_MATERIAL_EMBED_BORDER = 2,
	MESH_MATERIAL_EMBED_ICE = 3,
	MESH_MATERIAL_EMBED_RECHARGE = 4,
	MESH_MATERIAL_EMBED_DIRT = 5,
	MESH_MATERIAL_EMBED_LAVA = 6,
	MESH_MATERIAL_EMBED_HOLE = 7,
	MESH_MATERIAL_COUNT = 8,
};

struct TrackEditorMeshSurface {
	PackedVector3Array vertices;
	PackedVector3Array normals;
	PackedVector2Array uvs;
	PackedVector2Array uv2;
	PackedColorArray colors;
};

static const char *mesh_material_name(int p_material) {
	switch (p_material) {
		case MESH_MATERIAL_TRACK_RAIL:
			return "track_rail";
		case MESH_MATERIAL_EMBED_BORDER:
			return "embed_border";
		case MESH_MATERIAL_EMBED_ICE:
			return "embed_ice";
		case MESH_MATERIAL_EMBED_RECHARGE:
			return "embed_recharge";
		case MESH_MATERIAL_EMBED_DIRT:
			return "embed_dirt";
		case MESH_MATERIAL_EMBED_LAVA:
			return "embed_lava";
		case MESH_MATERIAL_EMBED_HOLE:
			return "embed_hole";
		case MESH_MATERIAL_TRACK_SURFACE:
		default:
			return "track_surface";
	}
}

static int mesh_material_for_embed_type(int p_embed_type) {
	switch (p_embed_type) {
		case 0:
			return MESH_MATERIAL_EMBED_RECHARGE;
		case 1:
			return MESH_MATERIAL_EMBED_DIRT;
		case 2:
			return MESH_MATERIAL_EMBED_ICE;
		case 3:
			return MESH_MATERIAL_EMBED_LAVA;
		case 4:
			return MESH_MATERIAL_EMBED_HOLE;
		default:
			return MESH_MATERIAL_EMBED_RECHARGE;
	}
}

static Color mesh_material_color(int p_material) {
	switch (p_material) {
		case MESH_MATERIAL_TRACK_RAIL:
			return Color(0.72f, 0.74f, 0.76f, 1.0f);
		case MESH_MATERIAL_EMBED_BORDER:
			return Color(0.96f, 0.92f, 0.78f, 1.0f);
		case MESH_MATERIAL_EMBED_ICE:
			return Color(0.36f, 0.78f, 1.0f, 1.0f);
		case MESH_MATERIAL_EMBED_RECHARGE:
			return Color(0.30f, 1.0f, 0.62f, 1.0f);
		case MESH_MATERIAL_EMBED_DIRT:
			return Color(0.56f, 0.40f, 0.24f, 1.0f);
		case MESH_MATERIAL_EMBED_LAVA:
			return Color(1.0f, 0.25f, 0.08f, 1.0f);
		case MESH_MATERIAL_EMBED_HOLE:
			return Color(0.02f, 0.02f, 0.025f, 1.0f);
		case MESH_MATERIAL_TRACK_SURFACE:
		default:
			return Color(0.50f, 0.52f, 0.54f, 1.0f);
	}
}

static inline void append_mesh_vertex(
	TrackEditorMeshSurface &r_surface,
	int p_material,
	const Vector3 &p_vertex,
	const Vector3 &p_normal,
	const Vector2 &p_uv,
	const Vector2 &p_uv2) {
	r_surface.vertices.append(p_vertex);
	r_surface.normals.append(p_normal);
	r_surface.uvs.append(p_uv);
	r_surface.uv2.append(p_uv2);
	r_surface.colors.append(mesh_material_color(p_material));
}

static inline void append_mesh_triangle(
	TrackEditorMeshSurface (&r_surfaces)[MESH_MATERIAL_COUNT],
	int p_material,
	const Vector3 &p_v0,
	const Vector3 &p_n0,
	const Vector2 &p_uv0,
	const Vector2 &p_uv20,
	const Vector3 &p_v1,
	const Vector3 &p_n1,
	const Vector2 &p_uv1,
	const Vector2 &p_uv21,
	const Vector3 &p_v2,
	const Vector3 &p_n2,
	const Vector2 &p_uv2,
	const Vector2 &p_uv22) {
	const int material = p_material >= 0 && p_material < MESH_MATERIAL_COUNT ? p_material : MESH_MATERIAL_TRACK_SURFACE;
	TrackEditorMeshSurface &surface = r_surfaces[material];
	append_mesh_vertex(surface, material, p_v0, p_n0, p_uv0, p_uv20);
	append_mesh_vertex(surface, material, p_v1, p_n1, p_uv1, p_uv21);
	append_mesh_vertex(surface, material, p_v2, p_n2, p_uv2, p_uv22);
}

static inline Vector3 normalized_or(const Vector3 &p_value, const Vector3 &p_fallback) {
	const real_t len2 = p_value.length_squared();
	if (len2 <= 0.0000001) {
		return p_fallback;
	}
	return p_value / Math::sqrt(len2);
}

static inline float signed_angle_to(const Vector3 &p_from, const Vector3 &p_to, const Vector3 &p_axis) {
	const Vector3 cross_to = p_from.cross(p_to);
	const float unsigned_angle = std::atan2((float)cross_to.length(), (float)clampf_local((float)p_from.dot(p_to), -1.0f, 1.0f));
	return cross_to.dot(p_axis) < 0.0f ? -unsigned_angle : unsigned_angle;
}

static inline Quaternion quat_from_to(const Vector3 &p_from, const Vector3 &p_to) {
	const Vector3 from = normalized_or(p_from, Vector3(0.0, 0.0, 1.0));
	const Vector3 to = normalized_or(p_to, Vector3(0.0, 0.0, 1.0));
	float dot = (float)from.dot(to);
	if (dot < -0.999999f) {
		Vector3 axis = Vector3(1.0, 0.0, 0.0).cross(from);
		if (axis.length_squared() < 0.000001) {
			axis = Vector3(0.0, 1.0, 0.0).cross(from);
		}
		return Quaternion(axis.normalized(), PI_F);
	}
	Vector3 cross = from.cross(to);
	Quaternion q(cross.x, cross.y, cross.z, 1.0f + dot);
	q.normalize();
	return q;
}

static inline float godot_ease(float p_t, float p_curve) {
	p_t = clampf_local(p_t, 0.0f, 1.0f);
	if (p_curve > 0.0f) {
		if (p_curve < 1.0f) {
			return 1.0f - std::pow(1.0f - p_t, 1.0f / p_curve);
		}
		return std::pow(p_t, p_curve);
	}
	if (p_curve < 0.0f) {
		if (p_t < 0.5f) {
			return std::pow(p_t * 2.0f, -p_curve) * 0.5f;
		}
		return (1.0f - std::pow(1.0f - ((p_t - 0.5f) * 2.0f), -p_curve)) * 0.5f + 0.5f;
	}
	return 0.0f;
}

static inline float remapped_ease_strength(float p_strength, int p_type) {
	switch (p_type) {
		case TrackEditorCurve::EASE_INOUT:
			return remapf_local(p_strength, 0.0f, 2.0f, -1.0f, -4.0f);
		case TrackEditorCurve::EASE_IN:
			return remapf_local(p_strength, 0.0f, 2.0f, 1.0f, 4.0f);
		case TrackEditorCurve::EASE_OUT:
			return remapf_local(p_strength, 0.0f, 2.0f, 1.0f, 0.01f);
		case TrackEditorCurve::EASE_LINEAR:
		default:
			return 1.0f;
	}
}

static inline float cp_value(const PackedFloat32Array &p_points, int p_index, int p_offset) {
	return p_points[p_index * TrackEditorCurve::CONTROL_STRIDE + p_offset];
}

static inline Vector3 cp_position(const PackedFloat32Array &p_points, int p_index) {
	const int base = p_index * TrackEditorCurve::CONTROL_STRIDE;
	return Vector3(p_points[base + 1], p_points[base + 2], p_points[base + 3]);
}

static inline Basis cp_basis(const PackedFloat32Array &p_points, int p_index) {
	const int base = p_index * TrackEditorCurve::CONTROL_STRIDE;
	return Basis(
		Vector3(p_points[base + 4], p_points[base + 5], p_points[base + 6]),
		Vector3(p_points[base + 7], p_points[base + 8], p_points[base + 9]),
		Vector3(p_points[base + 10], p_points[base + 11], p_points[base + 12])).orthonormalized();
}

static inline Vector3 cp_scale(const PackedFloat32Array &p_points, int p_index) {
	const int base = p_index * TrackEditorCurve::CONTROL_STRIDE;
	return Vector3(p_points[base + 13], p_points[base + 14], p_points[base + 15]);
}

static inline Vector3 cubic_bezier_pos(const Vector3 &p0, const Vector3 &p1, const Vector3 &p2, const Vector3 &p3, float p_t) {
	const float omt = 1.0f - p_t;
	const float omt2 = omt * omt;
	const float t2 = p_t * p_t;
	return p0 * (omt2 * omt) +
		p1 * (3.0f * omt2 * p_t) +
		p2 * (3.0f * omt * t2) +
		p3 * (t2 * p_t);
}

static inline Vector3 cubic_bezier_derivative(const Vector3 &p0, const Vector3 &p1, const Vector3 &p2, const Vector3 &p3, float p_t) {
	const float omt = 1.0f - p_t;
	return (p1 - p0) * (3.0f * omt * omt) +
		(p2 - p1) * (6.0f * omt * p_t) +
		(p3 - p2) * (3.0f * p_t * p_t);
}

static inline void set_basis_scale(Basis &p_basis, const Vector3 &p_scale) {
	p_basis.set_column(0, p_basis.get_column(0) * p_scale.x);
	p_basis.set_column(1, p_basis.get_column(1) * p_scale.y);
	p_basis.set_column(2, p_basis.get_column(2) * p_scale.z);
}

static inline Vector2 rounded_rect_point(float p_tx, float p_width, float p_height, float p_radius) {
	const float w2 = p_width * 0.5f;
	const float h2 = p_height * 0.5f;
	const float radius = clampf_local(p_radius, 0.0f, w2 < h2 ? w2 : h2);
	const float theta = p_tx * PI_F;
	const Vector2 dir(std::sin(theta), std::cos(theta));
	const float abs_dx = std::fabs((float)dir.x);
	const float abs_dy = std::fabs((float)dir.y);
	float best = 1.0e30f;

	if (abs_dx > 1.0e-9f) {
		const float tv = w2 / abs_dx;
		if (tv * abs_dy <= h2 - radius + 1.0e-9f) {
			best = best < tv ? best : tv;
		}
	}
	if (abs_dy > 1.0e-9f) {
		const float th = h2 / abs_dy;
		if (th * abs_dx <= w2 - radius + 1.0e-9f) {
			best = best < th ? best : th;
		}
	}
	if (radius > 1.0e-9f) {
		const float win = w2 - radius > 0.0f ? w2 - radius : 0.0f;
		const float hin = h2 - radius > 0.0f ? h2 - radius : 0.0f;
		const float b = -2.0f * (abs_dx * win + abs_dy * hin);
		const float c = win * win + hin * hin - radius * radius;
		const float disc = b * b - 4.0f * c;
		if (disc >= 0.0f) {
			const float sqrt_disc = std::sqrt(disc);
			const float roots[2] = { (-b - sqrt_disc) * 0.5f, (-b + sqrt_disc) * 0.5f };
			for (int i = 0; i < 2; ++i) {
				const float root = roots[i];
				if (root > 0.0f && root * abs_dx >= win - 1.0e-9f && root * abs_dy >= hin - 1.0e-9f) {
					best = best < root ? best : root;
				}
			}
		}
	}
	if (best == 1.0e30f) {
		best = w2 < h2 ? w2 : h2;
	}
	return dir * best;
}

void TrackEditorCurve::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_control_points", "points"), &TrackEditorCurve::set_control_points);
	ClassDB::bind_method(D_METHOD("get_control_points"), &TrackEditorCurve::get_control_points);
	ClassDB::bind_method(D_METHOD("clear_control_points"), &TrackEditorCurve::clear_control_points);
	ClassDB::bind_method(
		D_METHOD(
			"insert_control_point",
			"index",
			"time",
			"position",
			"rotation",
			"scale",
			"handle_in",
			"handle_out",
			"rot_ease_type",
			"rot_ease_strength",
			"twist_ease_type",
			"twist_ease_strength",
			"scale_ease_type",
			"scale_ease_strength"),
		&TrackEditorCurve::insert_control_point,
		DEFVAL(EASE_INOUT),
		DEFVAL(1.0f),
		DEFVAL(EASE_INOUT),
		DEFVAL(1.0f),
		DEFVAL(EASE_INOUT),
		DEFVAL(1.0f));
	ClassDB::bind_method(D_METHOD("remove_control_point", "index"), &TrackEditorCurve::remove_control_point);
	ClassDB::bind_method(D_METHOD("set_control_point_time", "index", "time"), &TrackEditorCurve::set_control_point_time);
	ClassDB::bind_method(D_METHOD("set_control_point_transform", "index", "position", "rotation", "scale"), &TrackEditorCurve::set_control_point_transform);
	ClassDB::bind_method(D_METHOD("set_control_point_handles", "index", "handle_in", "handle_out"), &TrackEditorCurve::set_control_point_handles);
	ClassDB::bind_method(D_METHOD("get_control_point_position", "index"), &TrackEditorCurve::get_control_point_position);
	ClassDB::bind_method(D_METHOD("get_control_point_rotation", "index"), &TrackEditorCurve::get_control_point_rotation);
	ClassDB::bind_method(D_METHOD("get_control_point_scale", "index"), &TrackEditorCurve::get_control_point_scale);
	ClassDB::bind_method(D_METHOD("get_control_point_time", "index"), &TrackEditorCurve::get_control_point_time);
	ClassDB::bind_method(D_METHOD("get_control_point_handle_in", "index"), &TrackEditorCurve::get_control_point_handle_in);
	ClassDB::bind_method(D_METHOD("get_control_point_handle_out", "index"), &TrackEditorCurve::get_control_point_handle_out);
	ClassDB::bind_method(D_METHOD("sort_control_points_by_time"), &TrackEditorCurve::sort_control_points_by_time);
	ClassDB::bind_method(D_METHOD("set_curve_mode", "curve_mode"), &TrackEditorCurve::set_curve_mode);
	ClassDB::bind_method(D_METHOD("get_curve_mode"), &TrackEditorCurve::get_curve_mode);
	ClassDB::bind_method(D_METHOD("set_rotation_mode", "rotation_mode"), &TrackEditorCurve::set_rotation_mode);
	ClassDB::bind_method(D_METHOD("get_rotation_mode"), &TrackEditorCurve::get_rotation_mode);
	ClassDB::bind_method(D_METHOD("get_control_point_count"), &TrackEditorCurve::get_control_point_count);
	ClassDB::bind_method(D_METHOD("get_segment_length"), &TrackEditorCurve::get_segment_length);
	ClassDB::bind_method(D_METHOD("respace_control_point_times", "samples_per_span"), &TrackEditorCurve::respace_control_point_times, DEFVAL(8));
	ClassDB::bind_method(D_METHOD("build_centerline_points", "point_count"), &TrackEditorCurve::build_centerline_points);
	ClassDB::bind_method(
		D_METHOD(
			"rebuild_spiral_from_packets",
			"axis_transform",
			"spiral_axis",
			"spiral_degrees",
			"radius_curve",
			"height_curve",
			"twist_curve",
			"scale_x_curve",
			"scale_y_curve",
			"subdivisions"),
		&TrackEditorCurve::rebuild_spiral_from_packets,
		DEFVAL(64));
	ClassDB::bind_method(D_METHOD("sample_bezier", "t"), &TrackEditorCurve::sample_bezier);
	ClassDB::bind_method(D_METHOD("sample_linear", "t"), &TrackEditorCurve::sample_linear);
	ClassDB::bind_method(D_METHOD("sample_surface_position", "config"), &TrackEditorCurve::sample_surface_position);
	ClassDB::bind_method(D_METHOD("sample_surface_positions", "config"), &TrackEditorCurve::sample_surface_positions);
	ClassDB::bind_method(D_METHOD("sample_surface_local_positions", "config"), &TrackEditorCurve::sample_surface_local_positions);
	ClassDB::bind_method(D_METHOD("build_baked_curve_matrix", "subdivisions"), &TrackEditorCurve::build_baked_curve_matrix);
	ClassDB::bind_method(D_METHOD("build_preview_mesh_with_curves", "config"), &TrackEditorCurve::build_preview_mesh_with_curves);
	ClassDB::bind_method(
		D_METHOD(
			"build_preview_mesh",
			"shape_type",
			"horizontal_segments",
			"uv_multiplier",
			"mesh_subdivision_length",
			"mesh_subdivision_angle_radians",
			"openness",
			"rounded_width",
			"rounded_height",
			"rounded_radius",
			"rounded_open_rotation",
			"left_rail_height",
			"right_rail_height",
			"left_rail_start",
			"left_rail_end",
			"right_rail_start",
			"right_rail_end"),
		&TrackEditorCurve::build_preview_mesh);

	ADD_PROPERTY(PropertyInfo(Variant::PACKED_FLOAT32_ARRAY, "control_points"), "set_control_points", "get_control_points");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "curve_mode"), "set_curve_mode", "get_curve_mode");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "rotation_mode"), "set_rotation_mode", "get_rotation_mode");

	BIND_CONSTANT(CONTROL_STRIDE);

	BIND_ENUM_CONSTANT(ROAD_SHAPE_FLAT);
	BIND_ENUM_CONSTANT(ROAD_SHAPE_CYLINDER);
	BIND_ENUM_CONSTANT(ROAD_SHAPE_CYLINDER_OPEN);
	BIND_ENUM_CONSTANT(ROAD_SHAPE_PIPE);
	BIND_ENUM_CONSTANT(ROAD_SHAPE_PIPE_OPEN);
	BIND_ENUM_CONSTANT(ROAD_SHAPE_ROUNDED_RECT);
	BIND_ENUM_CONSTANT(ROAD_SHAPE_ROUNDED_RECT_OPEN);
	BIND_ENUM_CONSTANT(ROAD_SHAPE_TUNNEL);

	BIND_ENUM_CONSTANT(CURVE_MODE_BEZIER);
	BIND_ENUM_CONSTANT(CURVE_MODE_LINEAR);
	BIND_ENUM_CONSTANT(ROTATION_MODE_SMART);
	BIND_ENUM_CONSTANT(ROTATION_MODE_SIMPLE);
}

TrackEditorFloatCurve::TrackEditorFloatCurve() {
	add_point(Vector2(0.0, 0.0));
	add_point(Vector2(1.0, 0.0));
}

static inline float float_curve_default_handle_x(int p_handle_kind) {
	return p_handle_kind < 0 ? -0.05f : 0.05f;
}

static inline float float_curve_slope_from_handle(float p_dx, float p_dy) {
	return std::fabs(p_dx) > 0.000001f ? p_dy / p_dx : 0.0f;
}

void TrackEditorFloatCurve::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_points", "points"), &TrackEditorFloatCurve::set_points);
	ClassDB::bind_method(D_METHOD("get_points"), &TrackEditorFloatCurve::get_points);
	ClassDB::bind_method(D_METHOD("set_point_count", "count"), &TrackEditorFloatCurve::set_point_count);
	ClassDB::bind_method(D_METHOD("get_point_count"), &TrackEditorFloatCurve::get_point_count);
	ClassDB::bind_method(D_METHOD("clear_points"), &TrackEditorFloatCurve::clear_points);
	ClassDB::bind_method(D_METHOD("add_point", "position", "left_tangent", "right_tangent", "left_mode", "right_mode"), &TrackEditorFloatCurve::add_point, DEFVAL(0.0f), DEFVAL(0.0f), DEFVAL(0), DEFVAL(0));
	ClassDB::bind_method(D_METHOD("remove_point", "index"), &TrackEditorFloatCurve::remove_point);
	ClassDB::bind_method(D_METHOD("get_point_position", "index"), &TrackEditorFloatCurve::get_point_position);
	ClassDB::bind_method(D_METHOD("get_point_left_tangent", "index"), &TrackEditorFloatCurve::get_point_left_tangent);
	ClassDB::bind_method(D_METHOD("get_point_right_tangent", "index"), &TrackEditorFloatCurve::get_point_right_tangent);
	ClassDB::bind_method(D_METHOD("get_point_left_handle", "index"), &TrackEditorFloatCurve::get_point_left_handle);
	ClassDB::bind_method(D_METHOD("get_point_right_handle", "index"), &TrackEditorFloatCurve::get_point_right_handle);
	ClassDB::bind_method(D_METHOD("get_point_left_mode", "index"), &TrackEditorFloatCurve::get_point_left_mode);
	ClassDB::bind_method(D_METHOD("get_point_right_mode", "index"), &TrackEditorFloatCurve::get_point_right_mode);
	ClassDB::bind_method(D_METHOD("set_point_offset", "index", "offset"), &TrackEditorFloatCurve::set_point_offset);
	ClassDB::bind_method(D_METHOD("set_point_value", "index", "value"), &TrackEditorFloatCurve::set_point_value);
	ClassDB::bind_method(D_METHOD("set_point_left_tangent", "index", "tangent"), &TrackEditorFloatCurve::set_point_left_tangent);
	ClassDB::bind_method(D_METHOD("set_point_right_tangent", "index", "tangent"), &TrackEditorFloatCurve::set_point_right_tangent);
	ClassDB::bind_method(D_METHOD("set_point_left_handle", "index", "handle"), &TrackEditorFloatCurve::set_point_left_handle);
	ClassDB::bind_method(D_METHOD("set_point_right_handle", "index", "handle"), &TrackEditorFloatCurve::set_point_right_handle);
	ClassDB::bind_method(D_METHOD("set_point_left_mode", "index", "mode"), &TrackEditorFloatCurve::set_point_left_mode);
	ClassDB::bind_method(D_METHOD("set_point_right_mode", "index", "mode"), &TrackEditorFloatCurve::set_point_right_mode);
	ClassDB::bind_method(D_METHOD("sample", "t"), &TrackEditorFloatCurve::sample);
	ClassDB::bind_method(D_METHOD("build_sampled_points", "point_count"), &TrackEditorFloatCurve::build_sampled_points);
	ClassDB::bind_method(D_METHOD("find_open_pipe_t_from_relative_pos", "pos"), &TrackEditorFloatCurve::find_open_pipe_t_from_relative_pos);
	ClassDB::bind_method(D_METHOD("find_open_cylinder_t_from_relative_pos", "pos"), &TrackEditorFloatCurve::find_open_cylinder_t_from_relative_pos);
	ClassDB::bind_method(D_METHOD("build_packet"), &TrackEditorFloatCurve::build_packet);
	ClassDB::bind_method(D_METHOD("build_linear_x_packet"), &TrackEditorFloatCurve::build_linear_x_packet);

	ADD_PROPERTY(PropertyInfo(Variant::PACKED_FLOAT32_ARRAY, "points"), "set_points", "get_points");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "point_count", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NONE), "set_point_count", "get_point_count");
	BIND_CONSTANT(POINT_STRIDE);
	BIND_CONSTANT(LEGACY_POINT_STRIDE);
	BIND_ENUM_CONSTANT(FLOAT_CURVE_BEZIER);
	BIND_ENUM_CONSTANT(FLOAT_CURVE_LINEAR);
	BIND_ENUM_CONSTANT(FLOAT_CURVE_CONSTANT);
}

void TrackEditorFloatCurve::set_points(const PackedFloat32Array &p_points) {
	if (p_points.size() % POINT_STRIDE == 0) {
		points = p_points;
		return;
	}
	if (p_points.size() % LEGACY_POINT_STRIDE != 0) {
		points = p_points;
		return;
	}
	const int count = p_points.size() / LEGACY_POINT_STRIDE;
	points.resize(count * POINT_STRIDE);
	for (int i = 0; i < count; ++i) {
		const int in_base = i * LEGACY_POINT_STRIDE;
		const int out_base = i * POINT_STRIDE;
		const float left_dx = float_curve_default_handle_x(-1);
		const float right_dx = float_curve_default_handle_x(1);
		points.set(out_base + 0, p_points[in_base + 0]);
		points.set(out_base + 1, p_points[in_base + 1]);
		points.set(out_base + 2, left_dx);
		points.set(out_base + 3, left_dx * p_points[in_base + 2]);
		points.set(out_base + 4, right_dx);
		points.set(out_base + 5, right_dx * p_points[in_base + 3]);
		points.set(out_base + 6, (float)FLOAT_CURVE_BEZIER);
		points.set(out_base + 7, (float)FLOAT_CURVE_BEZIER);
	}
}

PackedFloat32Array TrackEditorFloatCurve::get_points() const {
	return points;
}

void TrackEditorFloatCurve::set_point_count(int p_count) {
	(void)p_count;
}

int TrackEditorFloatCurve::get_point_count() const {
	if (points.size() % POINT_STRIDE == 0) {
		return points.size() / POINT_STRIDE;
	}
	return points.size() / LEGACY_POINT_STRIDE;
}

void TrackEditorFloatCurve::clear_points() {
	points.clear();
}

void TrackEditorFloatCurve::add_point(const Vector2 &p_position, float p_left_tangent, float p_right_tangent, int p_left_mode, int p_right_mode) {
	const int insert_count = get_point_count();
	int insert_index = insert_count;
	for (int i = 0; i < insert_count; ++i) {
		if (p_position.x < points[i * POINT_STRIDE]) {
			insert_index = i;
			break;
		}
	}
	PackedFloat32Array new_points;
	new_points.resize(points.size() + POINT_STRIDE);
	for (int i = 0; i < insert_index * POINT_STRIDE; ++i) {
		new_points.set(i, points[i]);
	}
	const int base = insert_index * POINT_STRIDE;
	const float left_dx = float_curve_default_handle_x(-1);
	const float right_dx = float_curve_default_handle_x(1);
	new_points.set(base + 0, (float)p_position.x);
	new_points.set(base + 1, (float)p_position.y);
	new_points.set(base + 2, left_dx);
	new_points.set(base + 3, left_dx * p_left_tangent);
	new_points.set(base + 4, right_dx);
	new_points.set(base + 5, right_dx * p_right_tangent);
	new_points.set(base + 6, (float)p_left_mode);
	new_points.set(base + 7, (float)p_right_mode);
	for (int i = base; i < points.size(); ++i) {
		new_points.set(i + POINT_STRIDE, points[i]);
	}
	points = new_points;
}

void TrackEditorFloatCurve::remove_point(int p_index) {
	const int count = get_point_count();
	if (p_index < 0 || p_index >= count) {
		return;
	}
	PackedFloat32Array new_points;
	new_points.resize(points.size() - POINT_STRIDE);
	int out = 0;
	for (int i = 0; i < count; ++i) {
		if (i == p_index) {
			continue;
		}
		const int base = i * POINT_STRIDE;
		for (int c = 0; c < POINT_STRIDE; ++c) {
			new_points.set(out++, points[base + c]);
		}
	}
	points = new_points;
}

Vector2 TrackEditorFloatCurve::get_point_position(int p_index) const {
	if (p_index < 0 || p_index >= get_point_count()) {
		return Vector2();
	}
	const int base = p_index * POINT_STRIDE;
	return Vector2(points[base + 0], points[base + 1]);
}

float TrackEditorFloatCurve::get_point_left_tangent(int p_index) const {
	if (p_index < 0 || p_index >= get_point_count()) {
		return 0.0f;
	}
	const int base = p_index * POINT_STRIDE;
	return float_curve_slope_from_handle(points[base + 2], points[base + 3]);
}

float TrackEditorFloatCurve::get_point_right_tangent(int p_index) const {
	if (p_index < 0 || p_index >= get_point_count()) {
		return 0.0f;
	}
	const int base = p_index * POINT_STRIDE;
	return float_curve_slope_from_handle(points[base + 4], points[base + 5]);
}

Vector2 TrackEditorFloatCurve::get_point_left_handle(int p_index) const {
	if (p_index < 0 || p_index >= get_point_count()) {
		return Vector2(float_curve_default_handle_x(-1), 0.0f);
	}
	const int base = p_index * POINT_STRIDE;
	return Vector2(points[base + 2], points[base + 3]);
}

Vector2 TrackEditorFloatCurve::get_point_right_handle(int p_index) const {
	if (p_index < 0 || p_index >= get_point_count()) {
		return Vector2(float_curve_default_handle_x(1), 0.0f);
	}
	const int base = p_index * POINT_STRIDE;
	return Vector2(points[base + 4], points[base + 5]);
}

int TrackEditorFloatCurve::get_point_left_mode(int p_index) const {
	if (p_index < 0 || p_index >= get_point_count()) {
		return FLOAT_CURVE_BEZIER;
	}
	return (int)points[p_index * POINT_STRIDE + 6];
}

int TrackEditorFloatCurve::get_point_right_mode(int p_index) const {
	if (p_index < 0 || p_index >= get_point_count()) {
		return FLOAT_CURVE_BEZIER;
	}
	return (int)points[p_index * POINT_STRIDE + 7];
}

void TrackEditorFloatCurve::set_point_offset(int p_index, float p_offset) {
	if (p_index < 0 || p_index >= get_point_count()) {
		return;
	}
	points.set(p_index * POINT_STRIDE + 0, p_offset);
}

void TrackEditorFloatCurve::set_point_value(int p_index, float p_value) {
	if (p_index < 0 || p_index >= get_point_count()) {
		return;
	}
	points.set(p_index * POINT_STRIDE + 1, p_value);
}

void TrackEditorFloatCurve::set_point_left_tangent(int p_index, float p_tangent) {
	if (p_index < 0 || p_index >= get_point_count()) {
		return;
	}
	const int base = p_index * POINT_STRIDE;
	float dx = points[base + 2];
	if (std::fabs(dx) <= 0.000001f) {
		dx = float_curve_default_handle_x(-1);
		points.set(base + 2, dx);
	}
	points.set(base + 3, dx * p_tangent);
}

void TrackEditorFloatCurve::set_point_right_tangent(int p_index, float p_tangent) {
	if (p_index < 0 || p_index >= get_point_count()) {
		return;
	}
	const int base = p_index * POINT_STRIDE;
	float dx = points[base + 4];
	if (std::fabs(dx) <= 0.000001f) {
		dx = float_curve_default_handle_x(1);
		points.set(base + 4, dx);
	}
	points.set(base + 5, dx * p_tangent);
}

void TrackEditorFloatCurve::set_point_left_handle(int p_index, const Vector2 &p_handle) {
	if (p_index < 0 || p_index >= get_point_count()) {
		return;
	}
	const int base = p_index * POINT_STRIDE;
	points.set(base + 2, (float)p_handle.x);
	points.set(base + 3, (float)p_handle.y);
}

void TrackEditorFloatCurve::set_point_right_handle(int p_index, const Vector2 &p_handle) {
	if (p_index < 0 || p_index >= get_point_count()) {
		return;
	}
	const int base = p_index * POINT_STRIDE;
	points.set(base + 4, (float)p_handle.x);
	points.set(base + 5, (float)p_handle.y);
}

void TrackEditorFloatCurve::set_point_left_mode(int p_index, int p_mode) {
	if (p_index < 0 || p_index >= get_point_count()) {
		return;
	}
	points.set(p_index * POINT_STRIDE + 6, (float)p_mode);
}

void TrackEditorFloatCurve::set_point_right_mode(int p_index, int p_mode) {
	if (p_index < 0 || p_index >= get_point_count()) {
		return;
	}
	points.set(p_index * POINT_STRIDE + 7, (float)p_mode);
}

float TrackEditorFloatCurve::sample(float p_t) const {
	const int count = get_point_count();
	if (count <= 0) {
		return 0.0f;
	}
	if (count == 1) {
		return points[1];
	}
	if (p_t <= points[0]) {
		return points[1];
	}
	const int last_base = (count - 1) * POINT_STRIDE;
	if (p_t >= points[last_base]) {
		return points[last_base + 1];
	}
	int start = 0;
	for (int i = 0; i < count - 1; ++i) {
		if (p_t <= points[(i + 1) * POINT_STRIDE]) {
			start = i;
			break;
		}
	}
	const int base0 = start * POINT_STRIDE;
	const int base1 = (start + 1) * POINT_STRIDE;
	const float t0 = points[base0 + 0];
	const float t1 = points[base1 + 0];
	const float dt = t1 - t0;
	if (std::fabs(dt) <= 0.000001f) {
		return points[base1 + 1];
	}
	const float p0 = points[base0 + 1];
	const float p3 = points[base1 + 1];
	const int mode = (int)points[base0 + 7];
	if (mode == FLOAT_CURVE_CONSTANT) {
		return p0;
	}
	const float linear_u = (p_t - t0) / dt;
	if (mode == FLOAT_CURVE_LINEAR) {
		return p0 + (p3 - p0) * linear_u;
	}
	const float x0 = t0;
	const float y0 = p0;
	const float x1 = t0 + points[base0 + 4];
	const float y1 = p0 + points[base0 + 5];
	const float x2 = t1 + points[base1 + 2];
	const float y2 = p3 + points[base1 + 3];
	const float x3 = t1;
	const float y3 = p3;
	float lo = 0.0f;
	float hi = 1.0f;
	float u = linear_u;
	for (int i = 0; i < 16; ++i) {
		const float omt = 1.0f - u;
		const float x = x0 * omt * omt * omt +
			x1 * 3.0f * omt * omt * u +
			x2 * 3.0f * omt * u * u +
			x3 * u * u * u;
		if (x < p_t) {
			lo = u;
		} else {
			hi = u;
		}
		u = (lo + hi) * 0.5f;
	}
	const float omt = 1.0f - u;
	return y0 * omt * omt * omt +
		y1 * 3.0f * omt * omt * u +
		y2 * 3.0f * omt * u * u +
		y3 * u * u * u;
}

PackedVector2Array TrackEditorFloatCurve::build_sampled_points(int p_point_count) const {
	PackedVector2Array out;
	const int point_count = p_point_count > 2 ? p_point_count : 2;
	out.resize(point_count);
	for (int i = 0; i < point_count; ++i) {
		const float t = point_count > 1 ? (float)i / (float)(point_count - 1) : 0.0f;
		out.set(i, Vector2(t, sample(t)));
	}
	return out;
}

Vector2 TrackEditorFloatCurve::find_open_pipe_t_from_relative_pos(const Vector3 &p_pos) const {
	float tx = std::atan2(p_pos.y, p_pos.x) / PI_F;
	tx /= std::fmax(0.001f, sample(p_pos.z));
	return Vector2(tx, p_pos.z);
}

Vector2 TrackEditorFloatCurve::find_open_cylinder_t_from_relative_pos(const Vector3 &p_pos) const {
	float tx = 0.5f - (std::atan2(p_pos.y, p_pos.x) / PI_F);
	if (tx < -1.0f) {
		tx += 2.0f;
	}
	if (tx > 1.0f) {
		tx -= 2.0f;
	}
	tx /= std::fmax(0.001f, sample(p_pos.z));
	return Vector2(tx, p_pos.z);
}

PackedFloat32Array TrackEditorFloatCurve::build_packet() const {
	PackedFloat32Array out;
	const int count = get_point_count();
	out.resize(1 + count * POINT_STRIDE);
	out.set(0, -(float)count);
	for (int i = 0; i < count * POINT_STRIDE; ++i) {
		out.set(i + 1, points[i]);
	}
	return out;
}

PackedFloat32Array TrackEditorFloatCurve::build_linear_x_packet() const {
	PackedFloat32Array out;
	const int count = get_point_count();
	out.resize(1 + count * LEGACY_POINT_STRIDE);
	out.set(0, (float)count);
	for (int i = 0; i < count; ++i) {
		const int in_base = i * POINT_STRIDE;
		const int out_base = 1 + i * LEGACY_POINT_STRIDE;
		out.set(out_base + 0, points[in_base + 0]);
		out.set(out_base + 1, points[in_base + 1]);
		out.set(out_base + 2, float_curve_slope_from_handle(points[in_base + 2], points[in_base + 3]));
		out.set(out_base + 3, float_curve_slope_from_handle(points[in_base + 4], points[in_base + 5]));
	}
	return out;
}

static void write_control_point(
	PackedFloat32Array &p_points,
	int p_index,
	float p_time,
	const Vector3 &p_position,
	const Basis &p_rotation,
	const Vector3 &p_scale,
	float p_handle_in,
	float p_handle_out,
	int p_rot_ease_type,
	float p_rot_ease_strength,
	int p_twist_ease_type,
	float p_twist_ease_strength,
	int p_scale_ease_type,
	float p_scale_ease_strength) {
	const Basis basis = p_rotation.orthonormalized();
	const Vector3 bx = basis.get_column(0);
	const Vector3 by = basis.get_column(1);
	const Vector3 bz = basis.get_column(2);
	const int base = p_index * TrackEditorCurve::CONTROL_STRIDE;
	p_points.set(base + 0, p_time);
	p_points.set(base + 1, p_position.x);
	p_points.set(base + 2, p_position.y);
	p_points.set(base + 3, p_position.z);
	p_points.set(base + 4, bx.x);
	p_points.set(base + 5, bx.y);
	p_points.set(base + 6, bx.z);
	p_points.set(base + 7, by.x);
	p_points.set(base + 8, by.y);
	p_points.set(base + 9, by.z);
	p_points.set(base + 10, bz.x);
	p_points.set(base + 11, bz.y);
	p_points.set(base + 12, bz.z);
	p_points.set(base + 13, p_scale.x);
	p_points.set(base + 14, p_scale.y);
	p_points.set(base + 15, p_scale.z);
	p_points.set(base + 16, p_handle_in);
	p_points.set(base + 17, p_handle_out);
	p_points.set(base + 18, (float)p_rot_ease_type);
	p_points.set(base + 19, p_rot_ease_strength);
	p_points.set(base + 20, (float)p_twist_ease_type);
	p_points.set(base + 21, p_twist_ease_strength);
	p_points.set(base + 22, (float)p_scale_ease_type);
	p_points.set(base + 23, p_scale_ease_strength);
}

void TrackEditorCurve::set_control_points(const PackedFloat32Array &p_points) {
	control_points = p_points;
}

PackedFloat32Array TrackEditorCurve::get_control_points() const {
	return control_points;
}

void TrackEditorCurve::clear_control_points() {
	control_points.clear();
	segment_length = 0.0f;
}

void TrackEditorCurve::insert_control_point(
	int p_index,
	float p_time,
	const Vector3 &p_position,
	const Basis &p_rotation,
	const Vector3 &p_scale,
	float p_handle_in,
	float p_handle_out,
	int p_rot_ease_type,
	float p_rot_ease_strength,
	int p_twist_ease_type,
	float p_twist_ease_strength,
	int p_scale_ease_type,
	float p_scale_ease_strength) {
	const int count = get_control_point_count();
	const int index = p_index < 0 ? 0 : (p_index > count ? count : p_index);
	PackedFloat32Array next;
	next.resize((count + 1) * CONTROL_STRIDE);
	for (int i = 0; i < index * CONTROL_STRIDE; ++i) {
		next.set(i, control_points[i]);
	}
	for (int i = index * CONTROL_STRIDE; i < count * CONTROL_STRIDE; ++i) {
		next.set(i + CONTROL_STRIDE, control_points[i]);
	}
	control_points = next;
	write_control_point(control_points, index, p_time, p_position, p_rotation, p_scale, p_handle_in, p_handle_out, p_rot_ease_type, p_rot_ease_strength, p_twist_ease_type, p_twist_ease_strength, p_scale_ease_type, p_scale_ease_strength);
}

void TrackEditorCurve::remove_control_point(int p_index) {
	const int count = get_control_point_count();
	if (p_index < 0 || p_index >= count) {
		return;
	}
	PackedFloat32Array next;
	next.resize((count - 1) * CONTROL_STRIDE);
	for (int i = 0; i < p_index * CONTROL_STRIDE; ++i) {
		next.set(i, control_points[i]);
	}
	for (int i = (p_index + 1) * CONTROL_STRIDE; i < count * CONTROL_STRIDE; ++i) {
		next.set(i - CONTROL_STRIDE, control_points[i]);
	}
	control_points = next;
}

void TrackEditorCurve::set_control_point_time(int p_index, float p_time) {
	if (p_index < 0 || p_index >= get_control_point_count()) {
		return;
	}
	control_points.set(p_index * CONTROL_STRIDE, p_time);
}

void TrackEditorCurve::set_control_point_transform(int p_index, const Vector3 &p_position, const Basis &p_rotation, const Vector3 &p_scale) {
	if (p_index < 0 || p_index >= get_control_point_count()) {
		return;
	}
	const int base = p_index * CONTROL_STRIDE;
	const Basis basis = p_rotation.orthonormalized();
	const Vector3 bx = basis.get_column(0);
	const Vector3 by = basis.get_column(1);
	const Vector3 bz = basis.get_column(2);
	control_points.set(base + 1, p_position.x);
	control_points.set(base + 2, p_position.y);
	control_points.set(base + 3, p_position.z);
	control_points.set(base + 4, bx.x);
	control_points.set(base + 5, bx.y);
	control_points.set(base + 6, bx.z);
	control_points.set(base + 7, by.x);
	control_points.set(base + 8, by.y);
	control_points.set(base + 9, by.z);
	control_points.set(base + 10, bz.x);
	control_points.set(base + 11, bz.y);
	control_points.set(base + 12, bz.z);
	control_points.set(base + 13, p_scale.x);
	control_points.set(base + 14, p_scale.y);
	control_points.set(base + 15, p_scale.z);
}

void TrackEditorCurve::set_control_point_handles(int p_index, float p_handle_in, float p_handle_out) {
	if (p_index < 0 || p_index >= get_control_point_count()) {
		return;
	}
	const int base = p_index * CONTROL_STRIDE;
	control_points.set(base + 16, p_handle_in);
	control_points.set(base + 17, p_handle_out);
}

Vector3 TrackEditorCurve::get_control_point_position(int p_index) const {
	if (p_index < 0 || p_index >= get_control_point_count()) {
		return Vector3();
	}
	return cp_position(control_points, p_index);
}

Basis TrackEditorCurve::get_control_point_rotation(int p_index) const {
	if (p_index < 0 || p_index >= get_control_point_count()) {
		return Basis();
	}
	return cp_basis(control_points, p_index);
}

Vector3 TrackEditorCurve::get_control_point_scale(int p_index) const {
	if (p_index < 0 || p_index >= get_control_point_count()) {
		return Vector3(1.0f, 1.0f, 1.0f);
	}
	return cp_scale(control_points, p_index);
}

float TrackEditorCurve::get_control_point_time(int p_index) const {
	if (p_index < 0 || p_index >= get_control_point_count()) {
		return 0.0f;
	}
	return cp_value(control_points, p_index, 0);
}

float TrackEditorCurve::get_control_point_handle_in(int p_index) const {
	if (p_index < 0 || p_index >= get_control_point_count()) {
		return 0.0f;
	}
	return cp_value(control_points, p_index, 16);
}

float TrackEditorCurve::get_control_point_handle_out(int p_index) const {
	if (p_index < 0 || p_index >= get_control_point_count()) {
		return 0.0f;
	}
	return cp_value(control_points, p_index, 17);
}

void TrackEditorCurve::sort_control_points_by_time() {
	const int count = get_control_point_count();
	for (int i = 1; i < count; ++i) {
		float temp[CONTROL_STRIDE];
		for (int j = 0; j < CONTROL_STRIDE; ++j) {
			temp[j] = control_points[i * CONTROL_STRIDE + j];
		}
		int pos = i - 1;
		while (pos >= 0 && control_points[pos * CONTROL_STRIDE] > temp[0]) {
			for (int j = 0; j < CONTROL_STRIDE; ++j) {
				control_points.set((pos + 1) * CONTROL_STRIDE + j, control_points[pos * CONTROL_STRIDE + j]);
			}
			--pos;
		}
		for (int j = 0; j < CONTROL_STRIDE; ++j) {
			control_points.set((pos + 1) * CONTROL_STRIDE + j, temp[j]);
		}
	}
}

void TrackEditorCurve::set_curve_mode(int p_curve_mode) {
	curve_mode = p_curve_mode == CURVE_MODE_LINEAR ? CURVE_MODE_LINEAR : CURVE_MODE_BEZIER;
}

int TrackEditorCurve::get_curve_mode() const {
	return curve_mode;
}

void TrackEditorCurve::set_rotation_mode(int p_rotation_mode) {
	rotation_mode = p_rotation_mode == ROTATION_MODE_SIMPLE ? ROTATION_MODE_SIMPLE : ROTATION_MODE_SMART;
}

int TrackEditorCurve::get_rotation_mode() const {
	return rotation_mode;
}

int TrackEditorCurve::get_control_point_count() const {
	return control_points.size() / CONTROL_STRIDE;
}

float TrackEditorCurve::get_segment_length() const {
	return segment_length;
}

PackedFloat32Array TrackEditorCurve::respace_control_point_times(int p_samples_per_span) {
	const int count = get_control_point_count();
	if (count <= 0) {
		return control_points;
	}
	if (count == 1) {
		control_points.set(0, 0.0f);
		segment_length = 0.0f;
		return control_points;
	}
	if (count == 2) {
		control_points.set(0, 0.0f);
		control_points.set(CONTROL_STRIDE, 1.0f);
		segment_length = (float)cp_position(control_points, 0).distance_to(cp_position(control_points, 1));
		return control_points;
	}

	for (int i = 0; i < count; ++i) {
		control_points.set(i * CONTROL_STRIDE, (float)i / (float)(count - 1));
	}

	const int samples_per_span = p_samples_per_span > 2 ? p_samples_per_span : 2;
	Vector<float> dists;
	dists.resize(count);
	dists.write[0] = 0.0f;

	float total_dist = 0.0f;
	for (int span = 0; span < count - 1; ++span) {
		const float start_t = cp_value(control_points, span, 0);
		const float end_t = cp_value(control_points, span + 1, 0);
		Vector3 prev = cp_position(control_points, span);
		for (int sample = 0; sample < samples_per_span; ++sample) {
			const float ratio = samples_per_span > 1 ? (float)sample / (float)(samples_per_span - 1) : 1.0f;
			const float t = start_t + (end_t - start_t) * ratio;
			const Vector3 pos = sample_bezier(t).origin;
			total_dist += (float)prev.distance_to(pos);
			prev = pos;
		}
		dists.write[span + 1] = total_dist;
	}

	if (total_dist <= 0.000001f) {
		for (int i = 0; i < count; ++i) {
			control_points.set(i * CONTROL_STRIDE, (float)i / (float)(count - 1));
		}
		segment_length = 0.0f;
		return control_points;
	}

	for (int i = 0; i < count; ++i) {
		control_points.set(i * CONTROL_STRIDE, dists[i] / total_dist);
	}
	control_points.set(0, 0.0f);
	control_points.set((count - 1) * CONTROL_STRIDE, 1.0f);
	segment_length = total_dist;
	return control_points;
}

PackedVector3Array TrackEditorCurve::build_centerline_points(int p_point_count) const {
	PackedVector3Array out;
	const int count = get_control_point_count();
	if (count <= 0) {
		return out;
	}
	const int point_count = p_point_count > 2 ? p_point_count : 2;
	out.resize(point_count);
	for (int i = 0; i < point_count; ++i) {
		const float t = point_count > 1 ? (float)i / (float)(point_count - 1) : 0.0f;
		out.set(i, sample_bezier(t).origin);
	}
	return out;
}

Transform3D TrackEditorCurve::sample_bezier(float p_t) const {
	if (curve_mode == CURVE_MODE_LINEAR) {
		return sample_linear(p_t);
	}
	const int count = get_control_point_count();
	if (count <= 0) {
		return Transform3D();
	}
	if (count == 1) {
		Basis basis = cp_basis(control_points, 0);
		set_basis_scale(basis, cp_scale(control_points, 0));
		return Transform3D(basis, cp_position(control_points, 0));
	}

	p_t = clampf_local(p_t, 0.0f, 1.0f);
	int span = 0;
	for (int i = 0; i < count - 1; ++i) {
		if (cp_value(control_points, i + 1, 0) >= p_t) {
			span = i;
			break;
		}
		span = i;
	}

	const float t0 = cp_value(control_points, span, 0);
	const float t1 = cp_value(control_points, span + 1, 0);
	const float bt = clampf_local(remapf_local(p_t, t0, t1, 0.0f, 1.0f), 0.0f, 1.0f);
	const Vector3 start_pos = cp_position(control_points, span);
	const Vector3 end_pos = cp_position(control_points, span + 1);
	const Basis start_basis = cp_basis(control_points, span);
	const Basis end_basis = cp_basis(control_points, span + 1);
	const float handle_out = cp_value(control_points, span, 17);
	const float handle_in = cp_value(control_points, span + 1, 16);
	const Vector3 p0 = start_pos;
	const Vector3 p1 = start_pos + start_basis.get_column(2) * handle_out;
	const Vector3 p2 = end_pos - end_basis.get_column(2) * handle_in;
	const Vector3 p3 = end_pos;
	const Vector3 final_pos = cubic_bezier_pos(p0, p1, p2, p3, bt);
	Vector3 forward_dir = cubic_bezier_derivative(p0, p1, p2, p3, bt);
	if (forward_dir.length_squared() <= 0.0000001) {
		forward_dir = end_basis.get_column(2);
	}
	forward_dir.normalize();

	const float rot_ease = godot_ease(bt, remapped_ease_strength(cp_value(control_points, span, 19), (int)cp_value(control_points, span, 18)));
	const float twist_ease = godot_ease(bt, remapped_ease_strength(cp_value(control_points, span, 21), (int)cp_value(control_points, span, 20)));
	const float scale_ease = godot_ease(bt, remapped_ease_strength(cp_value(control_points, span, 23), (int)cp_value(control_points, span, 22)));
	const Quaternion start_quat = start_basis.get_rotation_quaternion();
	const Quaternion end_quat = end_basis.get_rotation_quaternion();
	Basis final_rot;
	if (rotation_mode == ROTATION_MODE_SIMPLE) {
		final_rot = Basis(start_quat.slerp(end_quat, rot_ease));
	} else {
		const Vector3 start_z = start_basis.get_column(2).normalized();
		const Vector3 end_z = end_basis.get_column(2).normalized();
		Quaternion to_forward = quat_from_to(start_z, forward_dir);
		final_rot = Basis(to_forward * start_quat);

		Quaternion base_to_forward_for_end = quat_from_to(start_z, end_z);
		Basis end_rot_fixed = Basis(base_to_forward_for_end * start_quat);
		Quaternion end_to_forward = quat_from_to(end_rot_fixed.get_column(2), end_z);
		end_rot_fixed = Basis(end_to_forward) * end_rot_fixed;

		const float twist_end = signed_angle_to(end_rot_fixed.get_column(1).normalized(), end_basis.get_column(1).normalized(), end_z);
		final_rot.rotate(forward_dir, twist_end * twist_ease);
	}

	Basis basis = final_rot.orthonormalized();
	set_basis_scale(basis, cp_scale(control_points, span).lerp(cp_scale(control_points, span + 1), scale_ease));
	return Transform3D(basis, final_pos);
}

Transform3D TrackEditorCurve::sample_linear(float p_t) const {
	const int count = get_control_point_count();
	if (count <= 0) {
		return Transform3D();
	}
	if (count == 1) {
		Basis basis = cp_basis(control_points, 0);
		set_basis_scale(basis, cp_scale(control_points, 0));
		return Transform3D(basis, cp_position(control_points, 0));
	}

	p_t = clampf_local(p_t, 0.0f, 1.0f);
	int span = 0;
	for (int i = 0; i < count - 1; ++i) {
		if (cp_value(control_points, i + 1, 0) >= p_t) {
			span = i;
			break;
		}
		span = i;
	}

	const float t0 = cp_value(control_points, span, 0);
	const float t1 = cp_value(control_points, span + 1, 0);
	const float u = clampf_local(remapf_local(p_t, t0, t1, 0.0f, 1.0f), 0.0f, 1.0f);
	const Vector3 pos = cp_position(control_points, span).lerp(cp_position(control_points, span + 1), u);
	const Basis start_basis = cp_basis(control_points, span);
	const Basis end_basis = cp_basis(control_points, span + 1);
	const Quaternion start_quat = start_basis.get_rotation_quaternion();
	const Quaternion end_quat = end_basis.get_rotation_quaternion();
	Basis basis(start_quat.slerp(end_quat, u));
	set_basis_scale(basis, cp_scale(control_points, span).lerp(cp_scale(control_points, span + 1), u));
	return Transform3D(basis, pos);
}

PackedFloat32Array TrackEditorCurve::build_baked_curve_matrix(int p_subdivisions) const {
	PackedFloat32Array out;
	const int count = get_control_point_count();
	if (count < 2) {
		for (int channel = 0; channel < 15; ++channel) {
			out.append(0.0f);
		}
		return out;
	}

	const int subdivisions = p_subdivisions > 1 ? p_subdivisions : 1;
	const int key_count = subdivisions + 1;
	Vector<float> times;
	Vector<float> values[15];
	times.resize(key_count);
	for (int channel = 0; channel < 15; ++channel) {
		values[channel].resize(key_count);
	}

	for (int i = 0; i < key_count; ++i) {
		const float t = (float)i / (float)subdivisions;
		const Transform3D transform = sample_bezier(t);
		Basis basis = transform.basis;
		const Vector3 basis_x = basis.get_column(0);
		const Vector3 basis_y = basis.get_column(1);
		const Vector3 basis_z = basis.get_column(2);
		const Vector3 scale(basis_x.length(), basis_y.length(), basis_z.length());
		if (scale.x > 0.000001) {
			basis.set_column(0, basis_x / scale.x);
		}
		if (scale.y > 0.000001) {
			basis.set_column(1, basis_y / scale.y);
		}
		if (scale.z > 0.000001) {
			basis.set_column(2, basis_z / scale.z);
		}
		basis.orthonormalize();

		times.write[i] = t;
		values[0].write[i] = transform.origin.x;
		values[1].write[i] = transform.origin.y;
		values[2].write[i] = transform.origin.z;
		values[3].write[i] = basis.get_column(0).x;
		values[4].write[i] = basis.get_column(0).y;
		values[5].write[i] = basis.get_column(0).z;
		values[6].write[i] = basis.get_column(1).x;
		values[7].write[i] = basis.get_column(1).y;
		values[8].write[i] = basis.get_column(1).z;
		values[9].write[i] = basis.get_column(2).x;
		values[10].write[i] = basis.get_column(2).y;
		values[11].write[i] = basis.get_column(2).z;
		values[12].write[i] = scale.x;
		values[13].write[i] = scale.y;
		values[14].write[i] = scale.z;
	}

	for (int channel = 0; channel < 15; ++channel) {
		out.append((float)key_count);
		for (int i = 0; i < key_count; ++i) {
			float tangent = 0.0f;
			if (key_count > 1) {
				if (i == 0) {
					const float dt = times[1] - times[0];
					tangent = std::fabs(dt) > 0.000001f ? (values[channel][1] - values[channel][0]) / dt : 0.0f;
				} else if (i == key_count - 1) {
					const float dt = times[i] - times[i - 1];
					tangent = std::fabs(dt) > 0.000001f ? (values[channel][i] - values[channel][i - 1]) / dt : 0.0f;
				} else {
					const float dt = times[i + 1] - times[i - 1];
					tangent = std::fabs(dt) > 0.000001f ? (values[channel][i + 1] - values[channel][i - 1]) / dt : 0.0f;
				}
			}
			out.append(times[i]);
			out.append(values[channel][i]);
			out.append(tangent);
			out.append(tangent);
		}
	}
	return out;
}

static inline int curve_packet_point_count(const PackedFloat32Array &p_packet, int p_cursor) {
	if (p_cursor < 0 || p_cursor >= p_packet.size()) {
		return 0;
	}
	const int count = (int)p_packet[p_cursor];
	return count < 0 ? -count : count;
}

static inline int curve_packet_stride(const PackedFloat32Array &p_packet, int p_cursor) {
	if (p_cursor < 0 || p_cursor >= p_packet.size()) {
		return TrackEditorFloatCurve::LEGACY_POINT_STRIDE;
	}
	return p_packet[p_cursor] < 0.0f ? TrackEditorFloatCurve::POINT_STRIDE : TrackEditorFloatCurve::LEGACY_POINT_STRIDE;
}

static inline int curve_packet_next(const PackedFloat32Array &p_packet, int p_cursor) {
	const int count = curve_packet_point_count(p_packet, p_cursor);
	return p_cursor + 1 + count * curve_packet_stride(p_packet, p_cursor);
}

static float curve_packet_sample(const PackedFloat32Array &p_packet, int p_cursor, float p_t, float p_default) {
	const int count = curve_packet_point_count(p_packet, p_cursor);
	const int stride = curve_packet_stride(p_packet, p_cursor);
	if (count <= 0 || p_cursor + 1 + count * stride > p_packet.size()) {
		return p_default;
	}
	const int data = p_cursor + 1;
	if (count == 1) {
		return p_packet[data + 1];
	}
	if (p_t <= p_packet[data]) {
		return p_packet[data + 1];
	}
	const int last = data + (count - 1) * stride;
	if (p_t >= p_packet[last]) {
		return p_packet[last + 1];
	}
	int start = 0;
	for (int i = 0; i < count - 1; ++i) {
		if (p_t <= p_packet[data + (i + 1) * stride]) {
			start = i;
			break;
		}
	}
	const int base0 = data + start * stride;
	const int base1 = data + (start + 1) * stride;
	const float t0 = p_packet[base0 + 0];
	const float t1 = p_packet[base1 + 0];
	const float dt = t1 - t0;
	if (std::fabs(dt) <= 0.000001f) {
		return p_packet[base1 + 1];
	}
	const float p0 = p_packet[base0 + 1];
	const float p3 = p_packet[base1 + 1];
	const float linear_u = (p_t - t0) / dt;
	if (stride == TrackEditorFloatCurve::POINT_STRIDE) {
		const int mode = (int)p_packet[base0 + 7];
		if (mode == TrackEditorFloatCurve::FLOAT_CURVE_CONSTANT) {
			return p0;
		}
		if (mode == TrackEditorFloatCurve::FLOAT_CURVE_LINEAR) {
			return p0 + (p3 - p0) * linear_u;
		}
		const float x0 = t0;
		const float y0 = p0;
		const float x1 = t0 + p_packet[base0 + 4];
		const float y1 = p0 + p_packet[base0 + 5];
		const float x2 = t1 + p_packet[base1 + 2];
		const float y2 = p3 + p_packet[base1 + 3];
		const float x3 = t1;
		const float y3 = p3;
		float lo = 0.0f;
		float hi = 1.0f;
		float u = linear_u;
		for (int i = 0; i < 16; ++i) {
			const float omt = 1.0f - u;
			const float x = x0 * omt * omt * omt +
				x1 * 3.0f * omt * omt * u +
				x2 * 3.0f * omt * u * u +
				x3 * u * u * u;
			if (x < p_t) {
				lo = u;
			} else {
				hi = u;
			}
			u = (lo + hi) * 0.5f;
		}
		const float omt = 1.0f - u;
		return y0 * omt * omt * omt +
			y1 * 3.0f * omt * omt * u +
			y2 * 3.0f * omt * u * u +
			y3 * u * u * u;
	}
	const float u = linear_u;
	const float handle_dist = dt * (1.0f / 3.0f);
	const float p1 = p0 + handle_dist * p_packet[base0 + 3];
	const float p2 = p3 - handle_dist * p_packet[base1 + 2];
	const float omt = 1.0f - u;
	return p0 * omt * omt * omt +
		p1 * 3.0f * omt * omt * u +
		p2 * 3.0f * omt * u * u +
		p3 * u * u * u;
}

static inline Vector3 spiral_axis_or_up(const Vector3 &p_axis) {
	if (p_axis.length_squared() <= 0.0000001) {
		return Vector3(0.0, 1.0, 0.0);
	}
	return p_axis.normalized();
}

static inline Vector3 perpendicular_to_spiral_axis(const Vector3 &p_axis) {
	Vector3 out(p_axis.y, -p_axis.x, 0.0);
	if (out.length_squared() <= 0.0000001) {
		out = Vector3(1.0, 0.0, 0.0);
	}
	return out.normalized();
}

static Transform3D canonical_spiral_transform(
	const Vector3 &p_axis,
	float p_spiral_radians,
	const PackedFloat32Array &p_radius_curve,
	const PackedFloat32Array &p_height_curve,
	const PackedFloat32Array &p_twist_curve,
	float p_t) {
	const Vector3 axis = spiral_axis_or_up(p_axis);
	const Vector3 perpendicular = perpendicular_to_spiral_axis(axis);
	const float radius = curve_packet_sample(p_radius_curve, 0, p_t, 50.0f);
	const float height = curve_packet_sample(p_height_curve, 0, p_t, 0.0f);
	const float angle = p_spiral_radians * p_t;
	const Basis rot(Quaternion(axis, angle));
	const Vector3 about = perpendicular * radius;
	const Vector3 pos = -rot.xform(about) + axis * height;
	Basis basis(rot);
	const float twist = curve_packet_sample(p_twist_curve, 0, p_t, 0.0f) * PI_F / 180.0f;
	const Vector3 twist_axis = normalized_or(basis.get_column(2), Vector3(0.0, 0.0, 1.0));
	if (twist_axis.length_squared() > 0.0000001) {
		basis = Basis(Quaternion(twist_axis, twist)) * basis;
	}
	float t2 = clampf_local(p_t + 0.001f, 0.0f, 1.0f);
	bool sample_backward = false;
	if (std::fabs(t2 - p_t) <= 0.000001f) {
		t2 = clampf_local(p_t - 0.001f, 0.0f, 1.0f);
		sample_backward = true;
	}
	const float radius2 = curve_packet_sample(p_radius_curve, 0, t2, 100.0f);
	const float height2 = curve_packet_sample(p_height_curve, 0, t2, 0.0f);
	const float angle2 = p_spiral_radians * t2;
	const Vector3 pos2 = -Basis(Quaternion(axis, angle2)).xform(perpendicular * radius2) + axis * height2;
	const Vector3 delta = sample_backward ? pos - pos2 : pos2 - pos;
	if (delta.length_squared() > 0.0000001) {
		const Vector3 tangent = delta.normalized();
		const Vector3 current_z = normalized_or(basis.get_column(2), Vector3(0.0, 0.0, 1.0));
		const Vector3 axis_x = normalized_or(basis.get_column(0), Vector3(1.0, 0.0, 0.0));
		Vector3 z_proj = current_z - axis_x * current_z.dot(axis_x);
		Vector3 tan_proj = tangent - axis_x * tangent.dot(axis_x);
		if (z_proj.length_squared() > 0.0000001 && tan_proj.length_squared() > 0.0000001) {
			z_proj.normalize();
			tan_proj.normalize();
			float adjust_angle = std::atan2((float)z_proj.cross(tan_proj).length(), (float)clampf_local((float)z_proj.dot(tan_proj), -1.0f, 1.0f));
			if (z_proj.cross(tan_proj).dot(axis_x) < 0.0f) {
				adjust_angle = -adjust_angle;
			}
			basis = Basis(Quaternion(axis_x, adjust_angle)) * basis;
		}
	}
	return Transform3D(basis, pos);
}

float TrackEditorCurve::rebuild_spiral_from_packets(
	const Transform3D &p_axis_transform,
	const Vector3 &p_spiral_axis,
	float p_spiral_degrees,
	const PackedFloat32Array &p_radius_curve,
	const PackedFloat32Array &p_height_curve,
	const PackedFloat32Array &p_twist_curve,
	const PackedFloat32Array &p_scale_x_curve,
	const PackedFloat32Array &p_scale_y_curve,
	int p_subdivisions) {
	const int subdivisions = p_subdivisions > 1 ? p_subdivisions : 1;
	const int point_count = subdivisions + 1;
	const float spiral_radians = p_spiral_degrees * PI_F / 180.0f;
	const Vector3 axis = spiral_axis_or_up(p_spiral_axis);
	const Transform3D raw_start = canonical_spiral_transform(axis, spiral_radians, p_radius_curve, p_height_curve, p_twist_curve, 0.0f);
	const Transform3D correction = p_axis_transform * raw_start.affine_inverse();
	const float start_height = curve_packet_sample(p_height_curve, 0, 0.0f, 0.0f);
	const Vector3 world_height_axis = normalized_or(p_axis_transform.basis.xform(axis), Vector3(0.0, 1.0, 0.0));
	control_points.resize(point_count * CONTROL_STRIDE);
	curve_mode = CURVE_MODE_LINEAR;
	segment_length = 0.0f;
	Vector3 prev;
	for (int i = 0; i < point_count; ++i) {
		const float t = (float)i / (float)subdivisions;
		Transform3D transform = correction * canonical_spiral_transform(axis, spiral_radians, p_radius_curve, p_height_curve, p_twist_curve, t);
		transform.origin += world_height_axis * start_height;
		const Vector3 scale(
			curve_packet_sample(p_scale_x_curve, 0, t, 25.0f),
			curve_packet_sample(p_scale_y_curve, 0, t, 25.0f),
			1.0f);
		write_control_point(control_points, i, t, transform.origin, transform.basis.orthonormalized(), scale, 0.0f, 0.0f, EASE_LINEAR, 1.0f, EASE_LINEAR, 1.0f, EASE_LINEAR, 1.0f);
		if (i > 0) {
			segment_length += (float)prev.distance_to(transform.origin);
		}
		prev = transform.origin;
	}
	return segment_length;
}

static float sample_modulation_offset(const PackedFloat32Array &p_modulation_curves, float p_tx, float p_ty) {
	if (p_modulation_curves.size() <= 0) {
		return 0.0f;
	}
	int cursor = 1;
	const int count = (int)p_modulation_curves[0];
	float out = 0.0f;
	const float mod_t = 0.5f * (1.0f - p_tx);
	for (int i = 0; i < count; ++i) {
		const int effect_cursor = cursor;
		const int height_cursor = curve_packet_next(p_modulation_curves, effect_cursor);
		if (height_cursor >= p_modulation_curves.size()) {
			break;
		}
		const float effect = curve_packet_sample(p_modulation_curves, effect_cursor, p_ty, 0.0f);
		const float height = curve_packet_sample(p_modulation_curves, height_cursor, mod_t, 0.0f);
		out += effect * height;
		cursor = curve_packet_next(p_modulation_curves, height_cursor);
	}
	return out;
}

static void append_curve_key_times(PackedFloat32Array &r_rows, const PackedFloat32Array &p_packet, int p_cursor, float p_start, float p_end) {
	const int count = curve_packet_point_count(p_packet, p_cursor);
	const int stride = curve_packet_stride(p_packet, p_cursor);
	if (count <= 0 || p_cursor + 1 + count * stride > p_packet.size()) {
		return;
	}
	const int data = p_cursor + 1;
	const float span = p_end - p_start;
	for (int i = 0; i < count; ++i) {
		const float embed_t = p_packet[data + i * stride];
		const float ty = p_start + span * embed_t;
		if (ty > p_start && ty < p_end) {
			r_rows.append(ty);
		}
	}
}

static void append_curve_key_times_in_range(PackedFloat32Array &r_rows, const PackedFloat32Array &p_packet, int p_cursor, float p_start, float p_end) {
	const int count = curve_packet_point_count(p_packet, p_cursor);
	const int stride = curve_packet_stride(p_packet, p_cursor);
	if (count <= 0 || p_cursor + 1 + count * stride > p_packet.size()) {
		return;
	}
	const int data = p_cursor + 1;
	for (int i = 0; i < count; ++i) {
		const float ty = p_packet[data + i * stride];
		if (ty > p_start && ty < p_end) {
			r_rows.append(ty);
		}
	}
}

static void append_curve_key_times_full_range(PackedFloat32Array &r_rows, const PackedFloat32Array &p_packet) {
	append_curve_key_times(r_rows, p_packet, 0, 0.0f, 1.0f);
}

static void append_modulation_effect_key_times(PackedFloat32Array &r_rows, const PackedFloat32Array &p_modulation_curves) {
	if (p_modulation_curves.size() <= 0) {
		return;
	}
	int cursor = 1;
	const int count = (int)p_modulation_curves[0];
	for (int i = 0; i < count; ++i) {
		const int effect_cursor = cursor;
		const int height_cursor = curve_packet_next(p_modulation_curves, effect_cursor);
		if (height_cursor >= p_modulation_curves.size()) {
			break;
		}
		append_curve_key_times(r_rows, p_modulation_curves, effect_cursor, 0.0f, 1.0f);
		cursor = curve_packet_next(p_modulation_curves, height_cursor);
	}
}

static PackedFloat32Array unique_sorted_rows(PackedFloat32Array p_rows) {
	PackedFloat32Array out;
	if (p_rows.size() <= 0) {
		return out;
	}
	p_rows.sort();
	float last = p_rows[0];
	out.append(last);
	for (int i = 1; i < p_rows.size(); ++i) {
		const float v = p_rows[i];
		if (std::fabs(v - last) > 0.000001f) {
			out.append(v);
			last = v;
		}
	}
	return out;
}

static float interp_distance_at_t(const PackedFloat32Array &p_y_times, const PackedFloat32Array &p_dists, float p_t) {
	if (p_y_times.size() <= 0 || p_dists.size() <= 0) {
		return 0.0f;
	}
	if (p_t <= p_y_times[0]) {
		return p_dists[0];
	}
	const int last = p_y_times.size() - 1;
	if (p_t >= p_y_times[last]) {
		return p_dists[last];
	}
	for (int i = 0; i < last; ++i) {
		const float t0 = p_y_times[i];
		const float t1 = p_y_times[i + 1];
		if (p_t <= t1) {
			const float u = remapf_local(p_t, t0, t1, 0.0f, 1.0f);
			return p_dists[i] + (p_dists[i + 1] - p_dists[i]) * u;
		}
	}
	return p_dists[last];
}

static bool embed_packet_contains_hole(const PackedFloat32Array &p_embed_curves, float p_tx, float p_ty) {
	if (p_embed_curves.size() <= 0) {
		return false;
	}
	int cursor = 1;
	const int embed_count = (int)p_embed_curves[0];
	for (int embed = 0; embed < embed_count; ++embed) {
		if (cursor + 3 > p_embed_curves.size()) {
			return false;
		}
		float start = clampf_local(p_embed_curves[cursor + 0], 0.0f, 1.0f);
		float end = clampf_local(p_embed_curves[cursor + 1], 0.0f, 1.0f);
		const int embed_type = (int)p_embed_curves[cursor + 2];
		cursor += 3;
		const int left_cursor = cursor;
		const int right_cursor = curve_packet_next(p_embed_curves, left_cursor);
		const int next_cursor = curve_packet_next(p_embed_curves, right_cursor);
		if (right_cursor >= p_embed_curves.size() || next_cursor > p_embed_curves.size()) {
			return false;
		}
		cursor = next_cursor;
		if (embed_type != 4) {
			continue;
		}
		if (end < start) {
			const float tmp = start;
			start = end;
			end = tmp;
		}
		if (p_ty < start || p_ty > end || end - start <= 0.000001f) {
			continue;
		}
		float left = curve_packet_sample(p_embed_curves, left_cursor, p_ty, 0.0f);
		float right = curve_packet_sample(p_embed_curves, right_cursor, p_ty, 0.0f);
		if (right < left) {
			const float tmp = left;
			left = right;
			right = tmp;
		}
		if (p_tx >= left && p_tx <= right) {
			return true;
		}
	}
	return false;
}

static void append_hole_boundary_x_values(PackedFloat32Array &r_x_values, const PackedFloat32Array &p_embed_curves, float p_ty) {
	if (p_embed_curves.size() <= 0) {
		return;
	}
	int cursor = 1;
	const int embed_count = (int)p_embed_curves[0];
	for (int embed = 0; embed < embed_count; ++embed) {
		if (cursor + 3 > p_embed_curves.size()) {
			return;
		}
		float start = clampf_local(p_embed_curves[cursor + 0], 0.0f, 1.0f);
		float end = clampf_local(p_embed_curves[cursor + 1], 0.0f, 1.0f);
		const int embed_type = (int)p_embed_curves[cursor + 2];
		cursor += 3;
		const int left_cursor = cursor;
		const int right_cursor = curve_packet_next(p_embed_curves, left_cursor);
		const int next_cursor = curve_packet_next(p_embed_curves, right_cursor);
		if (right_cursor >= p_embed_curves.size() || next_cursor > p_embed_curves.size()) {
			return;
		}
		cursor = next_cursor;
		if (embed_type != 4) {
			continue;
		}
		if (end < start) {
			const float tmp = start;
			start = end;
			end = tmp;
		}
		if (p_ty < start || p_ty > end || end - start <= 0.000001f) {
			continue;
		}
		float left = curve_packet_sample(p_embed_curves, left_cursor, p_ty, -1.0f);
		float right = curve_packet_sample(p_embed_curves, right_cursor, p_ty, 1.0f);
		if (right < left) {
			const float tmp = left;
			left = right;
			right = tmp;
		}
		r_x_values.append(clampf_local(left, -1.0f, 1.0f));
		r_x_values.append(clampf_local(right, -1.0f, 1.0f));
	}
}

static Vector3 local_shape_position(
	int p_shape_type,
	float p_tx,
	float p_ty,
	float p_openness,
	float p_width,
	float p_height,
	float p_radius,
	float p_open_rotation,
	const PackedFloat32Array &p_openness_curve,
	const PackedFloat32Array &p_rounded_width_curve,
	const PackedFloat32Array &p_rounded_height_curve,
	const PackedFloat32Array &p_rounded_radius_curve,
	const PackedFloat32Array &p_rounded_open_rotation_curve,
	const PackedFloat32Array &p_modulation_curves) {
	const float openness = curve_packet_sample(p_openness_curve, 0, p_ty, p_openness);
	const float rounded_width = curve_packet_sample(p_rounded_width_curve, 0, p_ty, p_width);
	const float rounded_height = curve_packet_sample(p_rounded_height_curve, 0, p_ty, p_height);
	const float rounded_radius = curve_packet_sample(p_rounded_radius_curve, 0, p_ty, p_radius);
	const float rounded_open_rotation = curve_packet_sample(p_rounded_open_rotation_curve, 0, p_ty, p_open_rotation);
	const float modulation = sample_modulation_offset(p_modulation_curves, p_tx, p_ty);
	switch (p_shape_type) {
		case TrackEditorCurve::ROAD_SHAPE_CYLINDER: {
			const float theta = p_tx * PI_F;
			return Vector3(std::sin(theta), std::cos(theta), 0.0) * (1.0f + modulation);
		}
		case TrackEditorCurve::ROAD_SHAPE_CYLINDER_OPEN: {
			const float theta = (p_tx * openness) * PI_F;
			return Vector3(std::sin(theta), std::cos(theta), 0.0) * (1.0f + modulation);
		}
		case TrackEditorCurve::ROAD_SHAPE_PIPE: {
			const float theta = (p_tx - 0.5f) * PI_F;
			return Vector3(std::cos(theta), std::sin(theta), 0.0) * (1.0f + modulation);
		}
		case TrackEditorCurve::ROAD_SHAPE_PIPE_OPEN: {
			const float theta = ((p_tx * openness) - 0.5f) * PI_F;
			return Vector3(std::cos(theta), std::sin(theta), 0.0) * (1.0f + modulation);
		}
		case TrackEditorCurve::ROAD_SHAPE_ROUNDED_RECT: {
			const Vector2 p = rounded_rect_point(1.0f - p_tx, rounded_width, rounded_height, rounded_radius);
			return Vector3(p.x, p.y, 0.0) * (1.0f + modulation);
		}
		case TrackEditorCurve::ROAD_SHAPE_ROUNDED_RECT_OPEN: {
			float mod_tx = p_tx * openness + rounded_open_rotation;
			if (mod_tx < -1.0f) {
				mod_tx += 2.0f;
			}
			if (mod_tx > 1.0f) {
				mod_tx -= 2.0f;
			}
			const Vector2 p = rounded_rect_point(1.0f - mod_tx, rounded_width, rounded_height, rounded_radius);
			return Vector3(p.x, p.y, 0.0) * (1.0f + modulation);
		}
		case TrackEditorCurve::ROAD_SHAPE_FLAT:
		default:
			return Vector3(p_tx, modulation, 0.0);
	}
}

static Vector3 surface_position(
	const TrackEditorCurve *p_curve,
	int p_shape_type,
	float p_tx,
	float p_ty,
	float p_openness,
	float p_width,
	float p_height,
	float p_radius,
	float p_open_rotation,
	const PackedFloat32Array &p_openness_curve,
	const PackedFloat32Array &p_rounded_width_curve,
	const PackedFloat32Array &p_rounded_height_curve,
	const PackedFloat32Array &p_rounded_radius_curve,
	const PackedFloat32Array &p_rounded_open_rotation_curve,
	const PackedFloat32Array &p_modulation_curves) {
	const Transform3D root = p_curve->sample_bezier(p_ty);
	return root.xform(local_shape_position(p_shape_type, p_tx, p_ty, p_openness, p_width, p_height, p_radius, p_open_rotation, p_openness_curve, p_rounded_width_curve, p_rounded_height_curve, p_rounded_radius_curve, p_rounded_open_rotation_curve, p_modulation_curves));
}

static Vector3 surface_normal(
	const TrackEditorCurve *p_curve,
	int p_shape_type,
	float p_tx,
	float p_ty,
	float p_openness,
	float p_width,
	float p_height,
	float p_radius,
	float p_open_rotation,
	const PackedFloat32Array &p_openness_curve,
	const PackedFloat32Array &p_rounded_width_curve,
	const PackedFloat32Array &p_rounded_height_curve,
	const PackedFloat32Array &p_rounded_radius_curve,
	const PackedFloat32Array &p_rounded_open_rotation_curve,
	const PackedFloat32Array &p_modulation_curves) {
	const Vector3 pos = surface_position(p_curve, p_shape_type, p_tx, p_ty, p_openness, p_width, p_height, p_radius, p_open_rotation, p_openness_curve, p_rounded_width_curve, p_rounded_height_curve, p_rounded_radius_curve, p_rounded_open_rotation_curve, p_modulation_curves);
	const float tx_side = p_tx < 0.0f ? p_tx + 0.002f : p_tx - 0.002f;
	const float ty_side = p_ty < 0.5f ?
		(p_ty + 0.002f < 1.0f ? p_ty + 0.002f : 1.0f) :
		(p_ty - 0.002f > 0.0f ? p_ty - 0.002f : 0.0f);
	const Vector3 pos_side = surface_position(p_curve, p_shape_type, tx_side, p_ty, p_openness, p_width, p_height, p_radius, p_open_rotation, p_openness_curve, p_rounded_width_curve, p_rounded_height_curve, p_rounded_radius_curve, p_rounded_open_rotation_curve, p_modulation_curves);
	const Vector3 pos_forward = surface_position(p_curve, p_shape_type, p_tx, ty_side, p_openness, p_width, p_height, p_radius, p_open_rotation, p_openness_curve, p_rounded_width_curve, p_rounded_height_curve, p_rounded_radius_curve, p_rounded_open_rotation_curve, p_modulation_curves);
	Vector3 tangent_x = p_tx < 0.0f ? (pos_side - pos) : (pos - pos_side);
	Vector3 tangent_y = p_ty < 0.5f ? (pos_forward - pos) : (pos - pos_forward);
	tangent_x = normalized_or(tangent_x, Vector3(1.0, 0.0, 0.0));
	tangent_y = normalized_or(tangent_y, Vector3(0.0, 0.0, 1.0));
	return normalized_or(tangent_y.cross(tangent_x), Vector3(0.0, 1.0, 0.0));
}

struct TrackEditorMeshBuildConfig {
	int shape_type = TrackEditorCurve::ROAD_SHAPE_FLAT;
	const PackedFloat32Array *x_segments = nullptr;
	float openness = 1.0f;
	float rounded_width = 1.0f;
	float rounded_height = 1.0f;
	float rounded_radius = 0.0f;
	float rounded_open_rotation = 0.0f;
	const PackedFloat32Array *openness_curve = nullptr;
	const PackedFloat32Array *rounded_width_curve = nullptr;
	const PackedFloat32Array *rounded_height_curve = nullptr;
	const PackedFloat32Array *rounded_radius_curve = nullptr;
	const PackedFloat32Array *rounded_open_rotation_curve = nullptr;
	const PackedFloat32Array *modulation_curves = nullptr;
};

static Vector<Vector3> build_mesh_row_positions(const TrackEditorCurve *p_curve, const TrackEditorMeshBuildConfig &p_config, float p_ty) {
	Vector<Vector3> row;
	if (p_config.x_segments == nullptr) {
		return row;
	}
	const int num_x = p_config.x_segments->size();
	row.resize(num_x);
	for (int x = 0; x < num_x; ++x) {
		const float tx = (*p_config.x_segments)[x] * 2.0f - 1.0f;
		row.write[x] = surface_position(
			p_curve,
			p_config.shape_type,
			tx,
			p_ty,
			p_config.openness,
			p_config.rounded_width,
			p_config.rounded_height,
			p_config.rounded_radius,
			p_config.rounded_open_rotation,
			*p_config.openness_curve,
			*p_config.rounded_width_curve,
			*p_config.rounded_height_curve,
			*p_config.rounded_radius_curve,
			*p_config.rounded_open_rotation_curve,
			*p_config.modulation_curves);
	}
	return row;
}

static bool mesh_rows_need_split(const Vector<Vector3> &p_row0, const Vector<Vector3> &p_row1, const Vector<Vector3> &p_mid_row, float p_max_len, float p_max_angle) {
	const int num_x = p_row0.size();
	for (int x = 0; x < num_x; ++x) {
		const Vector3 d0 = p_mid_row[x] - p_row0[x];
		const Vector3 d1 = p_row1[x] - p_mid_row[x];
		const float len0 = (float)d0.length();
		const float len1 = (float)d1.length();
		if (len0 > p_max_len || len1 > p_max_len) {
			return true;
		}
		const float denom = len0 * len1;
		if (denom > 0.000000001f) {
			const float dot = clampf_local((float)d0.dot(d1) / denom, -1.0f, 1.0f);
			if (std::acos(dot) > p_max_angle) {
				return true;
			}
		}
	}
	return false;
}

static void append_adaptive_mesh_rows(
	PackedFloat32Array &r_rows,
	const TrackEditorCurve *p_curve,
	const TrackEditorMeshBuildConfig &p_config,
	float p_t0,
	float p_t1,
	const Vector<Vector3> &p_row0,
	const Vector<Vector3> &p_row1,
	float p_max_len,
	float p_max_angle,
	int p_depth) {
	static constexpr int MAX_DEPTH = 24;
	static constexpr float MIN_DT = 0.0000001f;
	if (p_t1 - p_t0 <= MIN_DT || p_depth >= MAX_DEPTH) {
		r_rows.append(p_t1);
		return;
	}
	const float tm = (p_t0 + p_t1) * 0.5f;
	const Vector<Vector3> mid_row = build_mesh_row_positions(p_curve, p_config, tm);
	if (!mesh_rows_need_split(p_row0, p_row1, mid_row, p_max_len, p_max_angle)) {
		r_rows.append(p_t1);
		return;
	}
	append_adaptive_mesh_rows(r_rows, p_curve, p_config, p_t0, tm, p_row0, mid_row, p_max_len, p_max_angle, p_depth + 1);
	append_adaptive_mesh_rows(r_rows, p_curve, p_config, tm, p_t1, mid_row, p_row1, p_max_len, p_max_angle, p_depth + 1);
}

Vector3 TrackEditorCurve::sample_surface_position(const Dictionary &p_config) const {
	const Vector2 t = p_config.get("t", Vector2());
	return surface_position(
		this,
		(int)p_config.get("shape_type", ROAD_SHAPE_FLAT),
		(float)t.x,
		(float)t.y,
		(float)p_config.get("openness", 1.0f),
		(float)p_config.get("rounded_width", 1.0f),
		(float)p_config.get("rounded_height", 1.0f),
		(float)p_config.get("rounded_radius", 0.0f),
		(float)p_config.get("rounded_open_rotation", 0.0f),
		p_config.get("openness_curve", PackedFloat32Array()),
		p_config.get("rounded_width_curve", PackedFloat32Array()),
		p_config.get("rounded_height_curve", PackedFloat32Array()),
		p_config.get("rounded_radius_curve", PackedFloat32Array()),
		p_config.get("rounded_open_rotation_curve", PackedFloat32Array()),
		p_config.get("modulation_curves", PackedFloat32Array()));
}

PackedVector3Array TrackEditorCurve::sample_surface_positions(const Dictionary &p_config) const {
	const PackedVector2Array points = p_config.get("points", PackedVector2Array());
	PackedVector3Array out;
	out.resize(points.size());
	const int shape_type = (int)p_config.get("shape_type", ROAD_SHAPE_FLAT);
	const float openness = (float)p_config.get("openness", 1.0f);
	const float rounded_width = (float)p_config.get("rounded_width", 1.0f);
	const float rounded_height = (float)p_config.get("rounded_height", 1.0f);
	const float rounded_radius = (float)p_config.get("rounded_radius", 0.0f);
	const float rounded_open_rotation = (float)p_config.get("rounded_open_rotation", 0.0f);
	const PackedFloat32Array openness_curve = p_config.get("openness_curve", PackedFloat32Array());
	const PackedFloat32Array rounded_width_curve = p_config.get("rounded_width_curve", PackedFloat32Array());
	const PackedFloat32Array rounded_height_curve = p_config.get("rounded_height_curve", PackedFloat32Array());
	const PackedFloat32Array rounded_radius_curve = p_config.get("rounded_radius_curve", PackedFloat32Array());
	const PackedFloat32Array rounded_open_rotation_curve = p_config.get("rounded_open_rotation_curve", PackedFloat32Array());
	const PackedFloat32Array modulation_curves = p_config.get("modulation_curves", PackedFloat32Array());
	for (int i = 0; i < points.size(); ++i) {
		const Vector2 t = points[i];
		out.set(i, surface_position(
			this,
			shape_type,
			(float)t.x,
			(float)t.y,
			openness,
			rounded_width,
			rounded_height,
			rounded_radius,
			rounded_open_rotation,
			openness_curve,
			rounded_width_curve,
			rounded_height_curve,
			rounded_radius_curve,
			rounded_open_rotation_curve,
			modulation_curves));
	}
	return out;
}

PackedVector3Array TrackEditorCurve::sample_surface_local_positions(const Dictionary &p_config) const {
	const PackedVector2Array points = p_config.get("points", PackedVector2Array());
	PackedVector3Array out;
	out.resize(points.size());
	const int shape_type = (int)p_config.get("shape_type", ROAD_SHAPE_FLAT);
	const float openness = (float)p_config.get("openness", 1.0f);
	const float rounded_width = (float)p_config.get("rounded_width", 1.0f);
	const float rounded_height = (float)p_config.get("rounded_height", 1.0f);
	const float rounded_radius = (float)p_config.get("rounded_radius", 0.0f);
	const float rounded_open_rotation = (float)p_config.get("rounded_open_rotation", 0.0f);
	const PackedFloat32Array openness_curve = p_config.get("openness_curve", PackedFloat32Array());
	const PackedFloat32Array rounded_width_curve = p_config.get("rounded_width_curve", PackedFloat32Array());
	const PackedFloat32Array rounded_height_curve = p_config.get("rounded_height_curve", PackedFloat32Array());
	const PackedFloat32Array rounded_radius_curve = p_config.get("rounded_radius_curve", PackedFloat32Array());
	const PackedFloat32Array rounded_open_rotation_curve = p_config.get("rounded_open_rotation_curve", PackedFloat32Array());
	const PackedFloat32Array modulation_curves = p_config.get("modulation_curves", PackedFloat32Array());
	const bool radial_shape =
		shape_type == ROAD_SHAPE_CYLINDER ||
		shape_type == ROAD_SHAPE_CYLINDER_OPEN ||
		shape_type == ROAD_SHAPE_PIPE ||
		shape_type == ROAD_SHAPE_PIPE_OPEN;
	for (int i = 0; i < points.size(); ++i) {
		const Vector2 t = points[i];
		Vector3 local = local_shape_position(
			shape_type,
			(float)t.x,
			(float)t.y,
			openness,
			rounded_width,
			rounded_height,
			rounded_radius,
			rounded_open_rotation,
			openness_curve,
			rounded_width_curve,
			rounded_height_curve,
			rounded_radius_curve,
			rounded_open_rotation_curve,
			modulation_curves);
		const Vector3 root_scale = sample_bezier((float)t.y).basis.get_scale();
		float denom = radial_shape ? (root_scale.x > root_scale.y ? root_scale.x : root_scale.y) : root_scale.x;
		if (std::fabs(denom) <= 0.000001f) {
			denom = 1.0f;
		}
		const Vector3 scale_factor(root_scale.x / denom, root_scale.y / denom, root_scale.z / denom);
		local = Vector3(local.x * scale_factor.x, local.y * scale_factor.y, local.z * scale_factor.z);
		out.set(i, local);
	}
	return out;
}

Dictionary TrackEditorCurve::build_preview_mesh(
	int p_shape_type,
	const PackedFloat32Array &p_horizontal_segments,
	float p_uv_multiplier,
	float p_mesh_subdivision_length,
	float p_mesh_subdivision_angle_radians,
	float p_openness,
	float p_rounded_width,
	float p_rounded_height,
	float p_rounded_radius,
	float p_rounded_open_rotation,
	float p_left_rail_height,
	float p_right_rail_height,
	float p_left_rail_start,
	float p_left_rail_end,
	float p_right_rail_start,
	float p_right_rail_end) {
	return build_preview_mesh_full(
		p_shape_type,
		p_horizontal_segments,
		p_uv_multiplier,
		p_mesh_subdivision_length,
		p_mesh_subdivision_angle_radians,
		p_openness,
		p_rounded_width,
		p_rounded_height,
		p_rounded_radius,
		p_rounded_open_rotation,
		p_left_rail_height,
		p_right_rail_height,
		p_left_rail_start,
		p_left_rail_end,
		p_right_rail_start,
		p_right_rail_end,
		PackedFloat32Array(),
		PackedFloat32Array(),
		PackedFloat32Array(),
		PackedFloat32Array(),
		PackedFloat32Array(),
		PackedFloat32Array(),
		PackedFloat32Array());
}

Dictionary TrackEditorCurve::build_preview_mesh_with_curves(const Dictionary &p_config) {
	return build_preview_mesh_full(
		(int)p_config.get("shape_type", ROAD_SHAPE_FLAT),
		p_config.get("horizontal_segments", PackedFloat32Array()),
		(float)p_config.get("uv_multiplier", 1.0f),
		(float)p_config.get("mesh_subdivision_length", 30.0f),
		(float)p_config.get("mesh_subdivision_angle_radians", 0.052359877f),
		(float)p_config.get("openness", 1.0f),
		(float)p_config.get("rounded_width", 1.0f),
		(float)p_config.get("rounded_height", 1.0f),
		(float)p_config.get("rounded_radius", 0.0f),
		(float)p_config.get("rounded_open_rotation", 0.0f),
		(float)p_config.get("left_rail_height", 0.0f),
		(float)p_config.get("right_rail_height", 0.0f),
		(float)p_config.get("left_rail_start", 0.0f),
		(float)p_config.get("left_rail_end", 1.0f),
		(float)p_config.get("right_rail_start", 0.0f),
		(float)p_config.get("right_rail_end", 1.0f),
		p_config.get("openness_curve", PackedFloat32Array()),
		p_config.get("rounded_width_curve", PackedFloat32Array()),
		p_config.get("rounded_height_curve", PackedFloat32Array()),
		p_config.get("rounded_radius_curve", PackedFloat32Array()),
		p_config.get("rounded_open_rotation_curve", PackedFloat32Array()),
		p_config.get("modulation_curves", PackedFloat32Array()),
		p_config.get("embed_curves", PackedFloat32Array()));
}

Dictionary TrackEditorCurve::build_preview_mesh_full(
	int p_shape_type,
	const PackedFloat32Array &p_horizontal_segments,
	float p_uv_multiplier,
	float p_mesh_subdivision_length,
	float p_mesh_subdivision_angle_radians,
	float p_openness,
	float p_rounded_width,
	float p_rounded_height,
	float p_rounded_radius,
	float p_rounded_open_rotation,
	float p_left_rail_height,
	float p_right_rail_height,
	float p_left_rail_start,
	float p_left_rail_end,
	float p_right_rail_start,
	float p_right_rail_end,
	const PackedFloat32Array &p_openness_curve,
	const PackedFloat32Array &p_rounded_width_curve,
	const PackedFloat32Array &p_rounded_height_curve,
	const PackedFloat32Array &p_rounded_radius_curve,
	const PackedFloat32Array &p_rounded_open_rotation_curve,
	const PackedFloat32Array &p_modulation_curves,
	const PackedFloat32Array &p_embed_curves) {
	Dictionary out;
	PackedVector3Array triangle_vertices;
	PackedVector3Array triangle_normals;
	PackedVector2Array triangle_uvs;
	PackedVector2Array triangle_curvatures;
	PackedColorArray triangle_colors;
	TrackEditorMeshSurface mesh_surfaces[MESH_MATERIAL_COUNT];
	PackedFloat32Array y_times;
	PackedFloat32Array dists;

	if (get_control_point_count() < 2) {
		out["vertices"] = triangle_vertices;
		out["normals"] = triangle_normals;
		out["uvs"] = triangle_uvs;
		out["uv2"] = triangle_curvatures;
		out["colors"] = triangle_colors;
		out["surfaces"] = Array();
		out["y_times"] = y_times;
		out["dists"] = dists;
		out["segment_length"] = 0.0f;
		return out;
	}

	PackedFloat32Array x_segments = p_horizontal_segments;
	if (x_segments.size() < 2) {
		x_segments.clear();
		for (int i = 0; i < 5; ++i) {
			x_segments.append(0.25f * (float)i);
		}
	}
	x_segments.sort();

	const float max_len = p_mesh_subdivision_length > 0.0001f ? p_mesh_subdivision_length : 0.0001f;
	const float max_angle = p_mesh_subdivision_angle_radians > 0.0001f ? p_mesh_subdivision_angle_radians : 0.0001f;

	PackedFloat32Array mandatory_times;
	mandatory_times.append(0.0f);
	mandatory_times.append(1.0f);
	for (int i = 0; i < get_control_point_count(); ++i) {
		mandatory_times.append(clampf_local(get_control_point_time(i), 0.0f, 1.0f));
	}
	append_curve_key_times_full_range(mandatory_times, p_openness_curve);
	append_curve_key_times_full_range(mandatory_times, p_rounded_width_curve);
	append_curve_key_times_full_range(mandatory_times, p_rounded_height_curve);
	append_curve_key_times_full_range(mandatory_times, p_rounded_radius_curve);
	append_curve_key_times_full_range(mandatory_times, p_rounded_open_rotation_curve);
	append_modulation_effect_key_times(mandatory_times, p_modulation_curves);
	mandatory_times.append(clampf_local(p_left_rail_start, 0.0f, 1.0f));
	mandatory_times.append(clampf_local(p_left_rail_end, 0.0f, 1.0f));
	mandatory_times.append(clampf_local(p_right_rail_start, 0.0f, 1.0f));
	mandatory_times.append(clampf_local(p_right_rail_end, 0.0f, 1.0f));
	if (p_embed_curves.size() > 0) {
		int cursor = 1;
		const int embed_count = (int)p_embed_curves[0];
		for (int embed = 0; embed < embed_count; ++embed) {
			if (cursor + 3 > p_embed_curves.size()) {
				break;
			}
			float start = clampf_local(p_embed_curves[cursor + 0], 0.0f, 1.0f);
			float end = clampf_local(p_embed_curves[cursor + 1], 0.0f, 1.0f);
			cursor += 3;
			const int left_cursor = cursor;
			const int right_cursor = curve_packet_next(p_embed_curves, left_cursor);
			const int next_cursor = curve_packet_next(p_embed_curves, right_cursor);
			if (right_cursor >= p_embed_curves.size() || next_cursor > p_embed_curves.size()) {
				break;
			}
			if (end < start) {
				const float tmp = start;
				start = end;
				end = tmp;
			}
			mandatory_times.append(start);
			mandatory_times.append(end);
			append_curve_key_times_in_range(mandatory_times, p_embed_curves, left_cursor, start, end);
			append_curve_key_times_in_range(mandatory_times, p_embed_curves, right_cursor, start, end);
			cursor = next_cursor;
		}
	}
	mandatory_times = unique_sorted_rows(mandatory_times);
	TrackEditorMeshBuildConfig mesh_config;
	mesh_config.shape_type = p_shape_type;
	mesh_config.x_segments = &x_segments;
	mesh_config.openness = p_openness;
	mesh_config.rounded_width = p_rounded_width;
	mesh_config.rounded_height = p_rounded_height;
	mesh_config.rounded_radius = p_rounded_radius;
	mesh_config.rounded_open_rotation = p_rounded_open_rotation;
	mesh_config.openness_curve = &p_openness_curve;
	mesh_config.rounded_width_curve = &p_rounded_width_curve;
	mesh_config.rounded_height_curve = &p_rounded_height_curve;
	mesh_config.rounded_radius_curve = &p_rounded_radius_curve;
	mesh_config.rounded_open_rotation_curve = &p_rounded_open_rotation_curve;
	mesh_config.modulation_curves = &p_modulation_curves;

	y_times.clear();
	if (mandatory_times.size() <= 0) {
		y_times.append(0.0f);
		y_times.append(1.0f);
	} else {
		y_times.append(mandatory_times[0]);
		for (int i = 0; i < mandatory_times.size() - 1; ++i) {
			const float t0 = mandatory_times[i];
			const float t1 = mandatory_times[i + 1];
			if (t1 <= t0) {
				continue;
			}
			const Vector<Vector3> row0 = build_mesh_row_positions(this, mesh_config, t0);
			const Vector<Vector3> row1 = build_mesh_row_positions(this, mesh_config, t1);
			append_adaptive_mesh_rows(y_times, this, mesh_config, t0, t1, row0, row1, max_len, max_angle, 0);
		}
	}
	y_times = unique_sorted_rows(y_times);

	dists.clear();
	dists.append(0.0f);
	float total_dist = 0.0f;
	Vector3 prev_center = sample_bezier(y_times[0]).origin;
	for (int y = 1; y < y_times.size(); ++y) {
		const Vector3 cur_center = sample_bezier(y_times[y]).origin;
		total_dist += (float)cur_center.distance_to(prev_center);
		dists.append(total_dist);
		prev_center = cur_center;
	}
	segment_length = total_dist;

	const int num_x = x_segments.size();
	const int num_y = y_times.size();
	Vector<Vector3> vertices;
	Vector<Vector3> normals;
	Vector<Vector2> uvs;
	Vector<Vector2> curves;
	vertices.resize(num_x * num_y);
	normals.resize(num_x * num_y);
	uvs.resize(num_x * num_y);
	curves.resize(num_x * num_y);

	const float uv_multiplier = std::fabs(p_uv_multiplier) > 0.001f ? std::fabs(p_uv_multiplier) : 0.001f;
	const float uv_tile = 50.0f / uv_multiplier;
	const float total_uv = total_dist / uv_tile;
	const float snapped_uv = std::round(total_uv) > 1.0f ? std::round(total_uv) : 1.0f;
	const float uv_correction = total_uv > 0.000001f ? snapped_uv / total_uv : 1.0f;

	for (int y = 0; y < num_y; ++y) {
		const float ty = y_times[y];
		for (int x = 0; x < num_x; ++x) {
			const float tx = x_segments[x] * 2.0f - 1.0f;
			const Vector3 pos = surface_position(this, p_shape_type, tx, ty, p_openness, p_rounded_width, p_rounded_height, p_rounded_radius, p_rounded_open_rotation, p_openness_curve, p_rounded_width_curve, p_rounded_height_curve, p_rounded_radius_curve, p_rounded_open_rotation_curve, p_modulation_curves);
			const Vector3 normal = surface_normal(this, p_shape_type, tx, ty, p_openness, p_rounded_width, p_rounded_height, p_rounded_radius, p_rounded_open_rotation, p_openness_curve, p_rounded_width_curve, p_rounded_height_curve, p_rounded_radius_curve, p_rounded_open_rotation_curve, p_modulation_curves);
			const int idx = y * num_x + x;
			vertices.write[idx] = pos;
			normals.write[idx] = normal;
			uvs.write[idx] = Vector2(x_segments[x], (dists[y] / uv_tile) * uv_correction);
			curves.write[idx] = Vector2(0.0, 0.0);
		}
	}

	for (int y = 0; y < num_y - 1; ++y) {
		const float ty0 = y_times[y];
		const float ty1 = y_times[y + 1];
		const float mid_ty = (ty0 + ty1) * 0.5f;
		PackedFloat32Array interval_x;
		for (int x = 0; x < num_x; ++x) {
			interval_x.append(x_segments[x] * 2.0f - 1.0f);
		}
		append_hole_boundary_x_values(interval_x, p_embed_curves, ty0);
		append_hole_boundary_x_values(interval_x, p_embed_curves, mid_ty);
		append_hole_boundary_x_values(interval_x, p_embed_curves, ty1);
		interval_x = unique_sorted_rows(interval_x);
		for (int x = 0; x < interval_x.size() - 1; ++x) {
			const float tx0 = interval_x[x];
			const float tx1 = interval_x[x + 1];
			if (tx1 - tx0 <= 0.000001f) {
				continue;
			}
			const float mid_tx = (tx0 + tx1) * 0.5f;
			if (embed_packet_contains_hole(p_embed_curves, mid_tx, mid_ty)) {
				continue;
			}
			const Vector3 v00 = surface_position(this, p_shape_type, tx0, ty0, p_openness, p_rounded_width, p_rounded_height, p_rounded_radius, p_rounded_open_rotation, p_openness_curve, p_rounded_width_curve, p_rounded_height_curve, p_rounded_radius_curve, p_rounded_open_rotation_curve, p_modulation_curves);
			const Vector3 v10 = surface_position(this, p_shape_type, tx1, ty0, p_openness, p_rounded_width, p_rounded_height, p_rounded_radius, p_rounded_open_rotation, p_openness_curve, p_rounded_width_curve, p_rounded_height_curve, p_rounded_radius_curve, p_rounded_open_rotation_curve, p_modulation_curves);
			const Vector3 v01 = surface_position(this, p_shape_type, tx0, ty1, p_openness, p_rounded_width, p_rounded_height, p_rounded_radius, p_rounded_open_rotation, p_openness_curve, p_rounded_width_curve, p_rounded_height_curve, p_rounded_radius_curve, p_rounded_open_rotation_curve, p_modulation_curves);
			const Vector3 v11 = surface_position(this, p_shape_type, tx1, ty1, p_openness, p_rounded_width, p_rounded_height, p_rounded_radius, p_rounded_open_rotation, p_openness_curve, p_rounded_width_curve, p_rounded_height_curve, p_rounded_radius_curve, p_rounded_open_rotation_curve, p_modulation_curves);
			const Vector3 n00 = surface_normal(this, p_shape_type, tx0, ty0, p_openness, p_rounded_width, p_rounded_height, p_rounded_radius, p_rounded_open_rotation, p_openness_curve, p_rounded_width_curve, p_rounded_height_curve, p_rounded_radius_curve, p_rounded_open_rotation_curve, p_modulation_curves);
			const Vector3 n10 = surface_normal(this, p_shape_type, tx1, ty0, p_openness, p_rounded_width, p_rounded_height, p_rounded_radius, p_rounded_open_rotation, p_openness_curve, p_rounded_width_curve, p_rounded_height_curve, p_rounded_radius_curve, p_rounded_open_rotation_curve, p_modulation_curves);
			const Vector3 n01 = surface_normal(this, p_shape_type, tx0, ty1, p_openness, p_rounded_width, p_rounded_height, p_rounded_radius, p_rounded_open_rotation, p_openness_curve, p_rounded_width_curve, p_rounded_height_curve, p_rounded_radius_curve, p_rounded_open_rotation_curve, p_modulation_curves);
			const Vector3 n11 = surface_normal(this, p_shape_type, tx1, ty1, p_openness, p_rounded_width, p_rounded_height, p_rounded_radius, p_rounded_open_rotation, p_openness_curve, p_rounded_width_curve, p_rounded_height_curve, p_rounded_radius_curve, p_rounded_open_rotation_curve, p_modulation_curves);
			const Vector2 uv00((tx0 + 1.0f) * 0.5f, (dists[y] / uv_tile) * uv_correction);
			const Vector2 uv10((tx1 + 1.0f) * 0.5f, (dists[y] / uv_tile) * uv_correction);
			const Vector2 uv01((tx0 + 1.0f) * 0.5f, (dists[y + 1] / uv_tile) * uv_correction);
			const Vector2 uv11((tx1 + 1.0f) * 0.5f, (dists[y + 1] / uv_tile) * uv_correction);
			const Vector2 empty_curve(0.0f, 0.0f);

			append_mesh_triangle(mesh_surfaces, MESH_MATERIAL_TRACK_SURFACE,
				v10, n10, uv10, empty_curve,
				v01, n01, uv01, empty_curve,
				v00, n00, uv00, empty_curve);
			append_mesh_triangle(mesh_surfaces, MESH_MATERIAL_TRACK_SURFACE,
				v10, n10, uv10, empty_curve,
				v11, n11, uv11, empty_curve,
				v01, n01, uv01, empty_curve);
		}
	}

	const float rail_starts[2] = {
		p_left_rail_start < p_left_rail_end ? p_left_rail_start : p_left_rail_end,
		p_right_rail_start < p_right_rail_end ? p_right_rail_start : p_right_rail_end
	};
	const float rail_ends[2] = {
		p_left_rail_start > p_left_rail_end ? p_left_rail_start : p_left_rail_end,
		p_right_rail_start > p_right_rail_end ? p_right_rail_start : p_right_rail_end
	};
	const float rail_heights[2] = { p_left_rail_height, p_right_rail_height };
	const int rail_x[2] = { num_x - 1, 0 };
	for (int side = 0; side < 2; ++side) {
		if (rail_heights[side] <= 0.0f) {
			continue;
		}
		for (int y = 0; y < num_y - 1; ++y) {
			const float mid = (y_times[y] + y_times[y + 1]) * 0.5f;
			if (mid < rail_starts[side] || mid > rail_ends[side]) {
				continue;
			}
			const float rail_tx = side == 0 ? 1.0f : -1.0f;
			if (embed_packet_contains_hole(p_embed_curves, rail_tx, mid)) {
				continue;
			}
			const int b0 = y * num_x + rail_x[side];
			const int b1 = (y + 1) * num_x + rail_x[side];
			const Vector3 up0 = normals[b0] * rail_heights[side];
			const Vector3 up1 = normals[b1] * rail_heights[side];
			const Vector3 v0 = vertices[b0];
			const Vector3 v1 = vertices[b1];
			const Vector3 v2 = vertices[b1] + up1;
			const Vector3 v3 = vertices[b0] + up0;
			const Vector3 n = normalized_or((v1 - v0).cross(v3 - v0), normals[b0]);
			const Vector2 uv0(0.0, uvs[b0].y);
			const Vector2 uv1(0.0, uvs[b1].y);
			const Vector2 uv2(1.0, uvs[b1].y);
			const Vector2 uv3(1.0, uvs[b0].y);
			Vector3 face_v[6];
			Vector2 face_uv[6];
			if (side == 0) {
				const Vector3 tmp_v[6] = { v0, v1, v2, v0, v2, v3 };
				const Vector2 tmp_uv[6] = { uv0, uv1, uv2, uv0, uv2, uv3 };
				for (int i = 0; i < 6; ++i) {
					face_v[i] = tmp_v[i];
					face_uv[i] = tmp_uv[i];
				}
			} else {
				const Vector3 tmp_v[6] = { v0, v2, v1, v0, v3, v2 };
				const Vector2 tmp_uv[6] = { uv0, uv2, uv1, uv0, uv3, uv2 };
				for (int i = 0; i < 6; ++i) {
					face_v[i] = tmp_v[i];
					face_uv[i] = tmp_uv[i];
				}
			}
			append_mesh_triangle(mesh_surfaces, MESH_MATERIAL_TRACK_RAIL,
				face_v[0], n, face_uv[0], Vector2(0.0, 0.0),
				face_v[1], n, face_uv[1], Vector2(0.0, 0.0),
				face_v[2], n, face_uv[2], Vector2(0.0, 0.0));
			append_mesh_triangle(mesh_surfaces, MESH_MATERIAL_TRACK_RAIL,
				face_v[3], n, face_uv[3], Vector2(0.0, 0.0),
				face_v[4], n, face_uv[4], Vector2(0.0, 0.0),
				face_v[5], n, face_uv[5], Vector2(0.0, 0.0));
		}
	}

	if (p_embed_curves.size() > 0) {
		static constexpr float EMBED_INSET_UNITS = 1.0f;
		static constexpr float EMBED_PUSH_DISTANCE = 0.5f;
		static constexpr int EMBED_X_DIVS = 8;
		int cursor = 1;
		const int embed_count = (int)p_embed_curves[0];
		for (int embed = 0; embed < embed_count; ++embed) {
			if (cursor + 3 > p_embed_curves.size()) {
				break;
			}
			float start = clampf_local(p_embed_curves[cursor + 0], 0.0f, 1.0f);
			float end = clampf_local(p_embed_curves[cursor + 1], 0.0f, 1.0f);
			const int embed_type = (int)p_embed_curves[cursor + 2];
			cursor += 3;
			const int left_cursor = cursor;
			const int right_cursor = curve_packet_next(p_embed_curves, left_cursor);
			const int next_cursor = curve_packet_next(p_embed_curves, right_cursor);
			if (right_cursor >= p_embed_curves.size() || next_cursor > p_embed_curves.size()) {
				break;
			}
			cursor = next_cursor;
			if (embed_type == 4) {
				continue;
			}
			if (end < start) {
				const float tmp = start;
				start = end;
				end = tmp;
			}
			if (end - start <= 0.000001f) {
				continue;
			}

			PackedFloat32Array embed_rows;
			embed_rows.append(start);
			for (int y = 0; y < y_times.size(); ++y) {
				const float ty = y_times[y];
				if (ty > start && ty < end) {
					embed_rows.append(ty);
				}
			}
			append_curve_key_times_in_range(embed_rows, p_embed_curves, left_cursor, start, end);
			append_curve_key_times_in_range(embed_rows, p_embed_curves, right_cursor, start, end);
			embed_rows.append(end);
			embed_rows = unique_sorted_rows(embed_rows);
			const int embed_y_count = embed_rows.size();
			if (embed_y_count < 2) {
				continue;
			}

			Vector<Vector3> embed_vertices;
			Vector<Vector3> embed_normals;
			Vector<Vector2> embed_uvs;
			Vector<float> embed_lefts;
			Vector<float> embed_rights;
			embed_vertices.resize(embed_y_count * EMBED_X_DIVS);
			embed_normals.resize(embed_y_count * EMBED_X_DIVS);
			embed_uvs.resize(embed_y_count * EMBED_X_DIVS);
			embed_lefts.resize(embed_y_count);
			embed_rights.resize(embed_y_count);
			for (int y = 0; y < embed_y_count; ++y) {
				const float ty = embed_rows[y];
				float left = curve_packet_sample(p_embed_curves, left_cursor, ty, 0.0f);
				float right = curve_packet_sample(p_embed_curves, right_cursor, ty, 0.0f);
				if (right < left) {
					const float tmp = left;
					left = right;
					right = tmp;
				}
				embed_lefts.write[y] = left;
				embed_rights.write[y] = right;
				const float uv_y = (interp_distance_at_t(y_times, dists, ty) / uv_tile) * uv_correction;
				for (int x = 0; x < EMBED_X_DIVS; ++x) {
					const float ux = (float)x / (float)(EMBED_X_DIVS - 1);
					const float tx = left + (right - left) * ux;
					const int idx = y * EMBED_X_DIVS + x;
					embed_vertices.write[idx] = surface_position(this, p_shape_type, tx, ty, p_openness, p_rounded_width, p_rounded_height, p_rounded_radius, p_rounded_open_rotation, p_openness_curve, p_rounded_width_curve, p_rounded_height_curve, p_rounded_radius_curve, p_rounded_open_rotation_curve, p_modulation_curves);
					embed_normals.write[idx] = surface_normal(this, p_shape_type, tx, ty, p_openness, p_rounded_width, p_rounded_height, p_rounded_radius, p_rounded_open_rotation, p_openness_curve, p_rounded_width_curve, p_rounded_height_curve, p_rounded_radius_curve, p_rounded_open_rotation_curve, p_modulation_curves);
					embed_uvs.write[idx] = Vector2(ux, uv_y);
				}
			}

			Vector<Vector3> inset_vertices;
			Vector<Vector3> inset_normals;
			Vector<Vector2> inset_uvs;
			inset_vertices.resize(embed_y_count * EMBED_X_DIVS);
			inset_normals.resize(embed_y_count * EMBED_X_DIVS);
			inset_uvs.resize(embed_y_count * EMBED_X_DIVS);
			const float centerline_len = (float)sample_bezier(start).origin.distance_to(sample_bezier(end).origin);
			const float inset_t = centerline_len > 0.000001f ? clampf_local(EMBED_INSET_UNITS / centerline_len, 0.0f, 0.49f) : 0.0f;
			for (int y = 0; y < embed_y_count; ++y) {
				float inset_ty = embed_rows[y];
				if (y == 0) {
					inset_ty = clampf_local(start + inset_t, start, end);
				} else if (y == embed_y_count - 1) {
					inset_ty = clampf_local(end - inset_t, start, end);
				}
				float left = curve_packet_sample(p_embed_curves, left_cursor, inset_ty, 0.0f);
				float right = curve_packet_sample(p_embed_curves, right_cursor, inset_ty, 0.0f);
				if (right < left) {
					const float tmp = left;
					left = right;
					right = tmp;
				}
				const Vector3 left_pos = surface_position(this, p_shape_type, left, inset_ty, p_openness, p_rounded_width, p_rounded_height, p_rounded_radius, p_rounded_open_rotation, p_openness_curve, p_rounded_width_curve, p_rounded_height_curve, p_rounded_radius_curve, p_rounded_open_rotation_curve, p_modulation_curves);
				const Vector3 right_pos = surface_position(this, p_shape_type, right, inset_ty, p_openness, p_rounded_width, p_rounded_height, p_rounded_radius, p_rounded_open_rotation, p_openness_curve, p_rounded_width_curve, p_rounded_height_curve, p_rounded_radius_curve, p_rounded_open_rotation_curve, p_modulation_curves);
				const float width = (float)left_pos.distance_to(right_pos);
				const float tx_inset = width > 0.000001f ? clampf_local((right - left) * (EMBED_INSET_UNITS / width), 0.0f, std::fabs(right - left) * 0.49f) : 0.0f;
				const float inner_left = left + tx_inset;
				const float inner_right = right - tx_inset;
				const float uv_y = (interp_distance_at_t(y_times, dists, inset_ty) / uv_tile) * uv_correction;
				for (int x = 0; x < EMBED_X_DIVS; ++x) {
					const float ux = (float)x / (float)(EMBED_X_DIVS - 1);
					const float tx = inner_left + (inner_right - inner_left) * ux;
					const int idx = y * EMBED_X_DIVS + x;
					const Vector3 normal = surface_normal(this, p_shape_type, tx, inset_ty, p_openness, p_rounded_width, p_rounded_height, p_rounded_radius, p_rounded_open_rotation, p_openness_curve, p_rounded_width_curve, p_rounded_height_curve, p_rounded_radius_curve, p_rounded_open_rotation_curve, p_modulation_curves);
					inset_vertices.write[idx] = surface_position(this, p_shape_type, tx, inset_ty, p_openness, p_rounded_width, p_rounded_height, p_rounded_radius, p_rounded_open_rotation, p_openness_curve, p_rounded_width_curve, p_rounded_height_curve, p_rounded_radius_curve, p_rounded_open_rotation_curve, p_modulation_curves) + normal * EMBED_PUSH_DISTANCE;
					inset_normals.write[idx] = normal;
					inset_uvs.write[idx] = Vector2(ux, uv_y);
				}
			}

			const int embed_material = mesh_material_for_embed_type(embed_type);
			for (int y = 0; y < embed_y_count - 1; ++y) {
				for (int x = 0; x < EMBED_X_DIVS - 1; ++x) {
					const int i00 = y * EMBED_X_DIVS + x;
					const int i10 = y * EMBED_X_DIVS + x + 1;
					const int i01 = (y + 1) * EMBED_X_DIVS + x;
					const int i11 = (y + 1) * EMBED_X_DIVS + x + 1;
					const Vector2 embed_uv2((float)(embed_type + 1), 0.0f);
					const float ux_mid = ((float)x + 0.5f) / (float)(EMBED_X_DIVS - 1);
					const float row_mid_left = (embed_lefts[y] + embed_lefts[y + 1]) * 0.5f;
					const float row_mid_right = (embed_rights[y] + embed_rights[y + 1]) * 0.5f;
					const float mid_tx = row_mid_left + (row_mid_right - row_mid_left) * ux_mid;
					const float mid_ty = (embed_rows[y] + embed_rows[y + 1]) * 0.5f;
					if (embed_packet_contains_hole(p_embed_curves, mid_tx, mid_ty)) {
						continue;
					}
					append_mesh_triangle(mesh_surfaces, embed_material,
						inset_vertices[i10], inset_normals[i10], inset_uvs[i10], embed_uv2,
						inset_vertices[i01], inset_normals[i01], inset_uvs[i01], embed_uv2,
						inset_vertices[i00], inset_normals[i00], inset_uvs[i00], embed_uv2);
					append_mesh_triangle(mesh_surfaces, embed_material,
						inset_vertices[i10], inset_normals[i10], inset_uvs[i10], embed_uv2,
						inset_vertices[i11], inset_normals[i11], inset_uvs[i11], embed_uv2,
						inset_vertices[i01], inset_normals[i01], inset_uvs[i01], embed_uv2);
				}
			}

			const Vector2 border_uv2((float)(embed_type + 1), 1.0f);
			for (int y = 0; y < embed_y_count - 1; ++y) {
				const int o00 = y * EMBED_X_DIVS;
				const int o01 = (y + 1) * EMBED_X_DIVS;
				const int i00 = y * EMBED_X_DIVS;
				const int i01 = (y + 1) * EMBED_X_DIVS;
				const int o10 = y * EMBED_X_DIVS + EMBED_X_DIVS - 1;
				const int o11 = (y + 1) * EMBED_X_DIVS + EMBED_X_DIVS - 1;
				const int i10 = y * EMBED_X_DIVS + EMBED_X_DIVS - 1;
				const int i11 = (y + 1) * EMBED_X_DIVS + EMBED_X_DIVS - 1;
				const float mid_ty = (embed_rows[y] + embed_rows[y + 1]) * 0.5f;
				if (!embed_packet_contains_hole(p_embed_curves, (embed_lefts[y] + embed_lefts[y + 1]) * 0.5f, mid_ty)) {
					append_mesh_triangle(mesh_surfaces, MESH_MATERIAL_EMBED_BORDER,
						inset_vertices[i01], inset_normals[i01], inset_uvs[i01], border_uv2,
						embed_vertices[o00], embed_normals[o00], embed_uvs[o00], border_uv2,
						inset_vertices[i00], inset_normals[i00], inset_uvs[i00], border_uv2);
					append_mesh_triangle(mesh_surfaces, MESH_MATERIAL_EMBED_BORDER,
						inset_vertices[i01], inset_normals[i01], inset_uvs[i01], border_uv2,
						embed_vertices[o01], embed_normals[o01], embed_uvs[o01], border_uv2,
						embed_vertices[o00], embed_normals[o00], embed_uvs[o00], border_uv2);
				}
				if (!embed_packet_contains_hole(p_embed_curves, (embed_rights[y] + embed_rights[y + 1]) * 0.5f, mid_ty)) {
					append_mesh_triangle(mesh_surfaces, MESH_MATERIAL_EMBED_BORDER,
						embed_vertices[o10], embed_normals[o10], embed_uvs[o10], border_uv2,
						inset_vertices[i11], inset_normals[i11], inset_uvs[i11], border_uv2,
						inset_vertices[i10], inset_normals[i10], inset_uvs[i10], border_uv2);
					append_mesh_triangle(mesh_surfaces, MESH_MATERIAL_EMBED_BORDER,
						embed_vertices[o10], embed_normals[o10], embed_uvs[o10], border_uv2,
						embed_vertices[o11], embed_normals[o11], embed_uvs[o11], border_uv2,
						inset_vertices[i11], inset_normals[i11], inset_uvs[i11], border_uv2);
				}
			}
			for (int x = 0; x < EMBED_X_DIVS - 1; ++x) {
				const int o00 = x;
				const int o10 = x + 1;
				const int i00 = x;
				const int i10 = x + 1;
				const int last_row = (embed_y_count - 1) * EMBED_X_DIVS;
				const int o01 = last_row + x;
				const int o11 = last_row + x + 1;
				const int i01 = last_row + x;
				const int i11 = last_row + x + 1;
				const float start_mid_tx = (embed_lefts[0] + (embed_rights[0] - embed_lefts[0]) * ((float)x + 0.5f) / (float)(EMBED_X_DIVS - 1));
				const float end_mid_tx = (embed_lefts[embed_y_count - 1] + (embed_rights[embed_y_count - 1] - embed_lefts[embed_y_count - 1]) * ((float)x + 0.5f) / (float)(EMBED_X_DIVS - 1));
				const bool start_in_hole = embed_packet_contains_hole(p_embed_curves, start_mid_tx, embed_rows[0]);
				const bool end_in_hole = embed_packet_contains_hole(p_embed_curves, end_mid_tx, embed_rows[embed_y_count - 1]);
				if (!start_in_hole) {
					append_mesh_triangle(mesh_surfaces, MESH_MATERIAL_EMBED_BORDER,
						embed_vertices[o10], embed_normals[o10], embed_uvs[o10], border_uv2,
						inset_vertices[i00], inset_normals[i00], inset_uvs[i00], border_uv2,
						embed_vertices[o00], embed_normals[o00], embed_uvs[o00], border_uv2);
					append_mesh_triangle(mesh_surfaces, MESH_MATERIAL_EMBED_BORDER,
						embed_vertices[o10], embed_normals[o10], embed_uvs[o10], border_uv2,
						inset_vertices[i10], inset_normals[i10], inset_uvs[i10], border_uv2,
						inset_vertices[i00], inset_normals[i00], inset_uvs[i00], border_uv2);
				}
				if (!end_in_hole) {
					append_mesh_triangle(mesh_surfaces, MESH_MATERIAL_EMBED_BORDER,
						inset_vertices[i11], inset_normals[i11], inset_uvs[i11], border_uv2,
						embed_vertices[o01], embed_normals[o01], embed_uvs[o01], border_uv2,
						inset_vertices[i01], inset_normals[i01], inset_uvs[i01], border_uv2);
					append_mesh_triangle(mesh_surfaces, MESH_MATERIAL_EMBED_BORDER,
						inset_vertices[i11], inset_normals[i11], inset_uvs[i11], border_uv2,
						embed_vertices[o11], embed_normals[o11], embed_uvs[o11], border_uv2,
						embed_vertices[o01], embed_normals[o01], embed_uvs[o01], border_uv2);
				}
			}
		}
	}

	Array surface_outputs;
	for (int material = 0; material < MESH_MATERIAL_COUNT; ++material) {
		TrackEditorMeshSurface &surface = mesh_surfaces[material];
		if (surface.vertices.size() <= 0) {
			continue;
		}
		Dictionary surface_out;
		surface_out["material_id"] = material;
		surface_out["material_name"] = mesh_material_name(material);
		surface_out["vertices"] = surface.vertices;
		surface_out["normals"] = surface.normals;
		surface_out["uvs"] = surface.uvs;
		surface_out["uv2"] = surface.uv2;
		surface_out["colors"] = surface.colors;
		surface_outputs.append(surface_out);

		triangle_vertices.append_array(surface.vertices);
		triangle_normals.append_array(surface.normals);
		triangle_uvs.append_array(surface.uvs);
		triangle_curvatures.append_array(surface.uv2);
		triangle_colors.append_array(surface.colors);
	}

	out["vertices"] = triangle_vertices;
	out["normals"] = triangle_normals;
	out["uvs"] = triangle_uvs;
	out["uv2"] = triangle_curvatures;
	out["colors"] = triangle_colors;
	out["surfaces"] = surface_outputs;
	out["y_times"] = y_times;
	out["dists"] = dists;
	out["segment_length"] = segment_length;
	return out;
}

}
