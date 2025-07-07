#pragma once
#include <godot_cpp/variant/transform3d.hpp>
#include <godot_cpp/variant/vector3.hpp>
#include <godot_cpp/variant/quaternion.hpp>
#include <mxt_core/deterministic_fp.hpp>
#include <cstring>

class MtxStack {
private:
	static constexpr int kStackSize = 16;
	godot::Transform3D	stack[kStackSize];
public:
	godot::Transform3D* cur;
	MtxStack() {
		stack[0] = godot::Transform3D();
		cur = &stack[0];
	}
	void push() {
		godot::Transform3D* next = cur + 1;
		*next = *cur;
		cur = next;
	}
	void pop() {
		--cur;
	}
	void assign(const godot::Transform3D& m) {
		*cur = m;
	}
	void clear_translation() {
		cur->origin.zero();
	}
	godot::Vector3 transform_point(const godot::Vector3& p) const {
		return cur->xform(p);
	}
	godot::Vector3 inverse_transform_point(const godot::Vector3& p) const {
		return cur->xform_inv(p);
	}
	godot::Vector3 rotate_point(const godot::Vector3& p) const {
		return cur->basis.xform(p);
	}
	godot::Vector3 inverse_rotate_point(const godot::Vector3& p) const {
		return cur->basis.xform_inv(p);
	}
	static godot::Quaternion make_axis_angle_quat(const godot::Vector3& axis, float angle) {
		return godot::Quaternion(axis, angle);
	}
	void from_quat(const godot::Quaternion& q) {
		cur->basis = godot::Basis(q);
		cur->origin.zero();
	}
	void identity() {
		*cur = godot::Transform3D();
	}
	void premultiply(const godot::Transform3D& m) {
		*cur = m * *cur;
	}
	void multiply(const godot::Transform3D& m) {
		*cur = *cur * m;
	}
	void rotate_x(float angle_rad) {
		float c = deterministic_fp::cosf(angle_rad);
		float s = deterministic_fp::sinf(angle_rad);

		godot::Vector3 y = cur->basis.get_column(1);
		godot::Vector3 z = cur->basis.get_column(2);

		cur->basis.set_column(1, y * c + z * s);		// new Y
		cur->basis.set_column(2, z * c - y * s);		// new Z
	}

	void rotate_y(float angle_rad) {
		float c = deterministic_fp::cosf(angle_rad);
		float s = deterministic_fp::sinf(angle_rad);

		godot::Vector3 x = cur->basis.get_column(0);
		godot::Vector3 z = cur->basis.get_column(2);

		cur->basis.set_column(0, x * c - z * s);		// new X
		cur->basis.set_column(2, x * s + z * c);		// new Z
	}

	void rotate_z(float angle_rad) {
		float c = deterministic_fp::cosf(angle_rad);
		float s = deterministic_fp::sinf(angle_rad);

		godot::Vector3 x = cur->basis.get_column(0);
		godot::Vector3 y = cur->basis.get_column(1);

		cur->basis.set_column(0, x * c + y * s);		// new X
		cur->basis.set_column(1, y * c - x * s);		// new Y
	}
};
