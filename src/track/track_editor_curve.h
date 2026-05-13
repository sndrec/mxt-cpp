#pragma once

#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/templates/vector.hpp>
#include <godot_cpp/variant/basis.hpp>
#include <godot_cpp/variant/color.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_color_array.hpp>
#include <godot_cpp/variant/packed_float32_array.hpp>
#include <godot_cpp/variant/packed_vector2_array.hpp>
#include <godot_cpp/variant/packed_vector3_array.hpp>
#include <godot_cpp/variant/transform3d.hpp>

namespace godot {

class TrackEditorCurve : public Resource {
	GDCLASS(TrackEditorCurve, Resource);

public:
	enum RoadShapeType {
		ROAD_SHAPE_FLAT = 0,
		ROAD_SHAPE_CYLINDER = 1,
		ROAD_SHAPE_CYLINDER_OPEN = 2,
		ROAD_SHAPE_PIPE = 3,
		ROAD_SHAPE_PIPE_OPEN = 4,
		ROAD_SHAPE_ROUNDED_RECT = 5,
		ROAD_SHAPE_ROUNDED_RECT_OPEN = 6,
	};

	enum EaseType {
		EASE_INOUT = 0,
		EASE_IN = 1,
		EASE_OUT = 2,
		EASE_LINEAR = 3,
	};

	enum CurveMode {
		CURVE_MODE_BEZIER = 0,
		CURVE_MODE_LINEAR = 1,
	};

	enum RotationMode {
		ROTATION_MODE_SMART = 0,
		ROTATION_MODE_SIMPLE = 1,
	};

	static constexpr int CONTROL_STRIDE = 24;

private:
	PackedFloat32Array control_points;
	float segment_length = 0.0f;
	int curve_mode = CURVE_MODE_BEZIER;
	int rotation_mode = ROTATION_MODE_SMART;

	static void _bind_methods();

public:
	void set_control_points(const PackedFloat32Array &p_points);
	PackedFloat32Array get_control_points() const;
	void clear_control_points();
	void insert_control_point(
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
		float p_scale_ease_strength);
	void remove_control_point(int p_index);
	void set_control_point_time(int p_index, float p_time);
	void set_control_point_transform(int p_index, const Vector3 &p_position, const Basis &p_rotation, const Vector3 &p_scale);
	void set_control_point_handles(int p_index, float p_handle_in, float p_handle_out);
	Vector3 get_control_point_position(int p_index) const;
	Basis get_control_point_rotation(int p_index) const;
	Vector3 get_control_point_scale(int p_index) const;
	float get_control_point_time(int p_index) const;
	float get_control_point_handle_in(int p_index) const;
	float get_control_point_handle_out(int p_index) const;
	void sort_control_points_by_time();
	void set_curve_mode(int p_curve_mode);
	int get_curve_mode() const;
	void set_rotation_mode(int p_rotation_mode);
	int get_rotation_mode() const;

	int get_control_point_count() const;
	float get_segment_length() const;

	PackedFloat32Array respace_control_point_times(int p_samples_per_span);
	PackedVector3Array build_centerline_points(int p_point_count) const;
	float rebuild_spiral_from_packets(
		const Transform3D &p_axis_transform,
		const Vector3 &p_spiral_axis,
		float p_spiral_degrees,
		const PackedFloat32Array &p_radius_curve,
		const PackedFloat32Array &p_height_curve,
		const PackedFloat32Array &p_twist_curve,
		const PackedFloat32Array &p_scale_x_curve,
		const PackedFloat32Array &p_scale_y_curve,
		int p_subdivisions);
	Transform3D sample_bezier(float p_t) const;
	Transform3D sample_linear(float p_t) const;
	Vector3 sample_surface_position(const Dictionary &p_config) const;
	PackedVector3Array sample_surface_positions(const Dictionary &p_config) const;
	PackedVector3Array sample_surface_local_positions(const Dictionary &p_config) const;
	PackedFloat32Array build_baked_curve_matrix(int p_subdivisions) const;
	Dictionary build_preview_mesh_with_curves(const Dictionary &p_config);
	Dictionary build_preview_mesh(
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
		float p_right_rail_end);
	Dictionary build_preview_mesh_full(
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
		const PackedFloat32Array &p_embed_curves);
};

class TrackEditorFloatCurve : public Resource {
	GDCLASS(TrackEditorFloatCurve, Resource);

private:
	PackedFloat32Array points;

	static void _bind_methods();

public:
	enum FloatCurveMode {
		FLOAT_CURVE_BEZIER = 0,
		FLOAT_CURVE_LINEAR = 1,
		FLOAT_CURVE_CONSTANT = 2,
	};

	static constexpr int POINT_STRIDE = 8;
	static constexpr int LEGACY_POINT_STRIDE = 4;

	TrackEditorFloatCurve();

	void set_points(const PackedFloat32Array &p_points);
	PackedFloat32Array get_points() const;

	void set_point_count(int p_count);
	int get_point_count() const;

	void clear_points();
	void add_point(const Vector2 &p_position, float p_left_tangent = 0.0f, float p_right_tangent = 0.0f, int p_left_mode = 0, int p_right_mode = 0);
	void remove_point(int p_index);
	Vector2 get_point_position(int p_index) const;
	float get_point_left_tangent(int p_index) const;
	float get_point_right_tangent(int p_index) const;
	Vector2 get_point_left_handle(int p_index) const;
	Vector2 get_point_right_handle(int p_index) const;
	int get_point_left_mode(int p_index) const;
	int get_point_right_mode(int p_index) const;
	void set_point_offset(int p_index, float p_offset);
	void set_point_value(int p_index, float p_value);
	void set_point_left_tangent(int p_index, float p_tangent);
	void set_point_right_tangent(int p_index, float p_tangent);
	void set_point_left_handle(int p_index, const Vector2 &p_handle);
	void set_point_right_handle(int p_index, const Vector2 &p_handle);
	void set_point_left_mode(int p_index, int p_mode);
	void set_point_right_mode(int p_index, int p_mode);
	float sample(float p_t) const;
	PackedVector2Array build_sampled_points(int p_point_count) const;
	Vector2 find_open_pipe_t_from_relative_pos(const Vector3 &p_pos) const;
	Vector2 find_open_cylinder_t_from_relative_pos(const Vector3 &p_pos) const;
	PackedFloat32Array build_packet() const;
	PackedFloat32Array build_linear_x_packet() const;
};

}

VARIANT_ENUM_CAST(godot::TrackEditorCurve::RoadShapeType);
VARIANT_ENUM_CAST(godot::TrackEditorCurve::EaseType);
VARIANT_ENUM_CAST(godot::TrackEditorCurve::CurveMode);
VARIANT_ENUM_CAST(godot::TrackEditorCurve::RotationMode);
VARIANT_ENUM_CAST(godot::TrackEditorFloatCurve::FloatCurveMode);
