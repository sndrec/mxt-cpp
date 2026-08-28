#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#if defined(__SSE__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 1)
#define MXT_SIMD_SSE 1
#else
#define MXT_SIMD_SSE 0
#endif

#if MXT_SIMD_SSE
#include <xmmintrin.h>
#endif

struct SimFloat4 {
#if MXT_SIMD_SSE
	__m128 v;
	SimFloat4() : v(_mm_setzero_ps()) {}
	explicit SimFloat4(__m128 p_v) : v(p_v) {}
	explicit SimFloat4(float s) : v(_mm_set1_ps(s)) {}
	SimFloat4(float x, float y, float z, float w) : v(_mm_set_ps(w, z, y, x)) {}
#else
	float v[4];
	SimFloat4() : v{0.0f, 0.0f, 0.0f, 0.0f} {}
	explicit SimFloat4(float s) : v{s, s, s, s} {}
	SimFloat4(float x, float y, float z, float w) : v{x, y, z, w} {}
#endif
};

inline SimFloat4 sim_load4(const float* p)
{
#if MXT_SIMD_SSE
	return SimFloat4(_mm_loadu_ps(p));
#else
	return SimFloat4(p[0], p[1], p[2], p[3]);
#endif
}

inline void sim_store4(float* p, SimFloat4 a)
{
#if MXT_SIMD_SSE
	_mm_storeu_ps(p, a.v);
#else
	p[0] = a.v[0]; p[1] = a.v[1]; p[2] = a.v[2]; p[3] = a.v[3];
#endif
}

inline SimFloat4 operator+(SimFloat4 a, SimFloat4 b)
{
#if MXT_SIMD_SSE
	return SimFloat4(_mm_add_ps(a.v, b.v));
#else
	return SimFloat4(a.v[0] + b.v[0], a.v[1] + b.v[1], a.v[2] + b.v[2], a.v[3] + b.v[3]);
#endif
}

inline SimFloat4 operator-(SimFloat4 a, SimFloat4 b)
{
#if MXT_SIMD_SSE
	return SimFloat4(_mm_sub_ps(a.v, b.v));
#else
	return SimFloat4(a.v[0] - b.v[0], a.v[1] - b.v[1], a.v[2] - b.v[2], a.v[3] - b.v[3]);
#endif
}

inline SimFloat4 operator*(SimFloat4 a, SimFloat4 b)
{
#if MXT_SIMD_SSE
	return SimFloat4(_mm_mul_ps(a.v, b.v));
#else
	return SimFloat4(a.v[0] * b.v[0], a.v[1] * b.v[1], a.v[2] * b.v[2], a.v[3] * b.v[3]);
#endif
}

inline SimFloat4 operator/(SimFloat4 a, SimFloat4 b)
{
#if MXT_SIMD_SSE
	return SimFloat4(_mm_div_ps(a.v, b.v));
#else
	return SimFloat4(a.v[0] / b.v[0], a.v[1] / b.v[1], a.v[2] / b.v[2], a.v[3] / b.v[3]);
#endif
}

inline SimFloat4 sim_max4(SimFloat4 a, SimFloat4 b)
{
#if MXT_SIMD_SSE
	return SimFloat4(_mm_max_ps(a.v, b.v));
#else
	return SimFloat4(
		std::max(a.v[0], b.v[0]),
		std::max(a.v[1], b.v[1]),
		std::max(a.v[2], b.v[2]),
		std::max(a.v[3], b.v[3]));
#endif
}

inline SimFloat4 sim_min4(SimFloat4 a, SimFloat4 b)
{
#if MXT_SIMD_SSE
	return SimFloat4(_mm_min_ps(a.v, b.v));
#else
	return SimFloat4(
		std::min(a.v[0], b.v[0]),
		std::min(a.v[1], b.v[1]),
		std::min(a.v[2], b.v[2]),
		std::min(a.v[3], b.v[3]));
#endif
}

inline SimFloat4 sim_sqrt4(SimFloat4 a)
{
#if MXT_SIMD_SSE
	return SimFloat4(_mm_sqrt_ps(a.v));
#else
	return SimFloat4(sqrtf(a.v[0]), sqrtf(a.v[1]), sqrtf(a.v[2]), sqrtf(a.v[3]));
#endif
}

struct SimVec3x4 {
	SimFloat4 x;
	SimFloat4 y;
	SimFloat4 z;
	SimVec3x4() = default;
	SimVec3x4(SimFloat4 p_x, SimFloat4 p_y, SimFloat4 p_z) : x(p_x), y(p_y), z(p_z) {}
};

inline SimVec3x4 sim_load_vec3x4(const float* x, const float* y, const float* z, int index)
{
	return SimVec3x4(sim_load4(x + index), sim_load4(y + index), sim_load4(z + index));
}

inline void sim_store_vec3x4(float* x, float* y, float* z, int index, const SimVec3x4& v)
{
	sim_store4(x + index, v.x);
	sim_store4(y + index, v.y);
	sim_store4(z + index, v.z);
}

struct SimVec2 {
	float x;
	float y;
	SimVec2() : x(0.0f), y(0.0f) {}
	SimVec2(float p_x, float p_y) : x(p_x), y(p_y) {}
	float length_squared() const { return x * x + y * y; }
	float length() const { return sqrtf(length_squared()); }
	SimVec2 normalized() const {
		const float len = length();
		return len > 0.000001f ? (*this) * (1.0f / len) : SimVec2();
	}
	float dot(const SimVec2& v) const { return x * v.x + y * v.y; }
	float& operator[](int i) { return i == 0 ? x : y; }
	const float& operator[](int i) const { return i == 0 ? x : y; }
	SimVec2 operator+(const SimVec2& v) const { return SimVec2(x + v.x, y + v.y); }
	SimVec2 operator-(const SimVec2& v) const { return SimVec2(x - v.x, y - v.y); }
	SimVec2 operator-() const { return SimVec2(-x, -y); }
	SimVec2 operator*(float s) const { return SimVec2(x * s, y * s); }
	SimVec2 operator/(float s) const { return SimVec2(x / s, y / s); }
	SimVec2& operator+=(const SimVec2& v) { x += v.x; y += v.y; return *this; }
	SimVec2& operator-=(const SimVec2& v) { x -= v.x; y -= v.y; return *this; }
	SimVec2& operator*=(float s) { x *= s; y *= s; return *this; }
};

inline SimVec2 operator*(float s, const SimVec2& v) { return v * s; }

struct SimVec3 {
	float x;
	float y;
	float z;

	SimVec3() : x(0.0f), y(0.0f), z(0.0f) {}
	SimVec3(float p_x, float p_y, float p_z) : x(p_x), y(p_y), z(p_z) {}

	void zero() { x = 0.0f; y = 0.0f; z = 0.0f; }
	float length_squared() const { return x * x + y * y + z * z; }
	float length() const { return sqrtf(length_squared()); }
	float distance_to(const SimVec3& v) const { return (*this - v).length(); }
	float distance_squared_to(const SimVec3& v) const { return (*this - v).length_squared(); }
	float dot(const SimVec3& v) const { return x * v.x + y * v.y + z * v.z; }
	SimVec3 cross(const SimVec3& v) const {
		return SimVec3(
			y * v.z - z * v.y,
			z * v.x - x * v.z,
			x * v.y - y * v.x);
	}
	SimVec3 normalized() const {
		const float len = length();
		return len > 0.000001f ? (*this) * (1.0f / len) : SimVec3();
	}
	void normalize() {
		const float len = length();
		if (len > 0.000001f) {
			*this *= (1.0f / len);
		}
	}
	SimVec3 lerp(const SimVec3& v, float t) const {
		return SimVec3(x + (v.x - x) * t, y + (v.y - y) * t, z + (v.z - z) * t);
	}
	SimVec3 slide(const SimVec3& normal) const {
		return *this - normal * dot(normal);
	}

	float& operator[](int i) { return i == 0 ? x : (i == 1 ? y : z); }
	const float& operator[](int i) const { return i == 0 ? x : (i == 1 ? y : z); }
	SimVec3 operator+(const SimVec3& v) const { return SimVec3(x + v.x, y + v.y, z + v.z); }
	SimVec3 operator-(const SimVec3& v) const { return SimVec3(x - v.x, y - v.y, z - v.z); }
	SimVec3 operator-() const { return SimVec3(-x, -y, -z); }
	SimVec3 operator*(const SimVec3& v) const { return SimVec3(x * v.x, y * v.y, z * v.z); }
	SimVec3 operator*(float s) const { return SimVec3(x * s, y * s, z * s); }
	SimVec3 operator/(float s) const { return SimVec3(x / s, y / s, z / s); }
	SimVec3& operator+=(const SimVec3& v) { x += v.x; y += v.y; z += v.z; return *this; }
	SimVec3& operator-=(const SimVec3& v) { x -= v.x; y -= v.y; z -= v.z; return *this; }
	SimVec3& operator*=(float s) { x *= s; y *= s; z *= s; return *this; }
	bool is_equal_approx(const SimVec3& v) const {
		return fabsf(x - v.x) < 0.00001f && fabsf(y - v.y) < 0.00001f && fabsf(z - v.z) < 0.00001f;
	}
};

inline SimVec3 operator*(float s, const SimVec3& v) { return v * s; }

struct SimQuat {
	float x;
	float y;
	float z;
	float w;

	SimQuat() : x(0.0f), y(0.0f), z(0.0f), w(1.0f) {}
	SimQuat(float p_x, float p_y, float p_z, float p_w) : x(p_x), y(p_y), z(p_z), w(p_w) {}
	SimQuat(const SimVec3& axis, float angle) {
		const float half = angle * 0.5f;
		const float s = sinf(half);
		SimVec3 n = axis.normalized();
		x = n.x * s;
		y = n.y * s;
		z = n.z * s;
		w = cosf(half);
	}
	SimQuat(const SimVec3& from, const SimVec3& to) {
		SimVec3 f = from.normalized();
		SimVec3 t = to.normalized();
		float d = f.dot(t);
		if (d < -0.999999f) {
			SimVec3 axis = SimVec3(1.0f, 0.0f, 0.0f).cross(f);
			if (axis.length_squared() < 0.000001f) {
				axis = SimVec3(0.0f, 1.0f, 0.0f).cross(f);
			}
			*this = SimQuat(axis.normalized(), 3.1415926535897932f);
			return;
		}
		SimVec3 c = f.cross(t);
		x = c.x;
		y = c.y;
		z = c.z;
		w = 1.0f + d;
		normalize();
	}

	void normalize() {
		float len = sqrtf(x * x + y * y + z * z + w * w);
		if (len > 0.000001f) {
			float inv = 1.0f / len;
			x *= inv; y *= inv; z *= inv; w *= inv;
		}
	}

	SimQuat slerp(const SimQuat& b, float t) const {
		SimQuat to = b;
		float cosom = x * b.x + y * b.y + z * b.z + w * b.w;
		if (cosom < 0.0f) {
			cosom = -cosom;
			to.x = -to.x; to.y = -to.y; to.z = -to.z; to.w = -to.w;
		}
		float scale0;
		float scale1;
		if ((1.0f - cosom) > 0.000001f) {
			float omega = acosf(cosom);
			float sinom = sinf(omega);
			scale0 = sinf((1.0f - t) * omega) / sinom;
			scale1 = sinf(t * omega) / sinom;
		} else {
			scale0 = 1.0f - t;
			scale1 = t;
		}
		return SimQuat(
			scale0 * x + scale1 * to.x,
			scale0 * y + scale1 * to.y,
			scale0 * z + scale1 * to.z,
			scale0 * w + scale1 * to.w);
	}
};

struct SimBasis {
	SimVec3 c0;
	SimVec3 c1;
	SimVec3 c2;

	SimBasis() : c0(1.0f, 0.0f, 0.0f), c1(0.0f, 1.0f, 0.0f), c2(0.0f, 0.0f, 1.0f) {}
	SimBasis(float xx, float xy, float xz, float yx, float yy, float yz, float zx, float zy, float zz)
		: c0(xx, xy, xz), c1(yx, yy, yz), c2(zx, zy, zz) {}
	SimBasis(const SimQuat& q) {
		const float x2 = q.x + q.x;
		const float y2 = q.y + q.y;
		const float z2 = q.z + q.z;
		const float xx = q.x * x2;
		const float yy = q.y * y2;
		const float zz = q.z * z2;
		const float xy = q.x * y2;
		const float xz = q.x * z2;
		const float yz = q.y * z2;
		const float wx = q.w * x2;
		const float wy = q.w * y2;
		const float wz = q.w * z2;
		c0 = SimVec3(1.0f - (yy + zz), xy + wz, xz - wy);
		c1 = SimVec3(xy - wz, 1.0f - (xx + zz), yz + wx);
		c2 = SimVec3(xz + wy, yz - wx, 1.0f - (xx + yy));
	}

	SimVec3& operator[](int i) { return i == 0 ? c0 : (i == 1 ? c1 : c2); }
	const SimVec3& operator[](int i) const { return i == 0 ? c0 : (i == 1 ? c1 : c2); }
	SimVec3 get_column(int i) const { return (*this)[i]; }
	void set_column(int i, const SimVec3& v) { (*this)[i] = v; }

	SimVec3 xform(const SimVec3& p) const { return c0 * p.x + c1 * p.y + c2 * p.z; }
	SimVec3 xform_inv(const SimVec3& p) const { return SimVec3(c0.dot(p), c1.dot(p), c2.dot(p)); }

	SimBasis transposed() const {
		return SimBasis(c0.x, c1.x, c2.x, c0.y, c1.y, c2.y, c0.z, c1.z, c2.z);
	}
	void transpose() { *this = transposed(); }

	void orthonormalize() {
		c0.normalize();
		c1 = (c1 - c0 * c0.dot(c1)).normalized();
		c2 = c0.cross(c1).normalized();
	}

	SimBasis rotated(const SimVec3& axis, float angle) const {
		const SimBasis r(SimQuat(axis, angle));
		SimBasis out;
		out.c0 = r.xform(c0);
		out.c1 = r.xform(c1);
		out.c2 = r.xform(c2);
		return out;
	}

	SimQuat get_rotation_quaternion() const {
		const float trace = c0.x + c1.y + c2.z;
		SimQuat q;
		if (trace > 0.0f) {
			float s = sqrtf(trace + 1.0f) * 2.0f;
			q.w = 0.25f * s;
			q.x = (c1.z - c2.y) / s;
			q.y = (c2.x - c0.z) / s;
			q.z = (c0.y - c1.x) / s;
		} else if (c0.x > c1.y && c0.x > c2.z) {
			float s = sqrtf(1.0f + c0.x - c1.y - c2.z) * 2.0f;
			q.w = (c1.z - c2.y) / s;
			q.x = 0.25f * s;
			q.y = (c1.x + c0.y) / s;
			q.z = (c2.x + c0.z) / s;
		} else if (c1.y > c2.z) {
			float s = sqrtf(1.0f + c1.y - c0.x - c2.z) * 2.0f;
			q.w = (c2.x - c0.z) / s;
			q.x = (c1.x + c0.y) / s;
			q.y = 0.25f * s;
			q.z = (c2.y + c1.z) / s;
		} else {
			float s = sqrtf(1.0f + c2.z - c0.x - c1.y) * 2.0f;
			q.w = (c0.y - c1.x) / s;
			q.x = (c2.x + c0.z) / s;
			q.y = (c2.y + c1.z) / s;
			q.z = 0.25f * s;
		}
		q.normalize();
		return q;
	}
};

inline SimBasis operator*(const SimBasis& a, const SimBasis& b) {
	SimBasis out;
	out.c0 = a.xform(b.c0);
	out.c1 = a.xform(b.c1);
	out.c2 = a.xform(b.c2);
	return out;
}

struct SimTransform {
	SimBasis basis;
	SimVec3 origin;

	SimTransform() : basis(), origin() {}
	SimTransform(const SimBasis& p_basis, const SimVec3& p_origin) : basis(p_basis), origin(p_origin) {}

	SimVec3 xform(const SimVec3& p) const { return basis.xform(p) + origin; }
	SimVec3 xform_inv(const SimVec3& p) const { return basis.xform_inv(p - origin); }
	void orthonormalize() { basis.orthonormalize(); }
	SimTransform affine_inverse() const {
		SimTransform out;
		out.basis = basis.transposed();
		out.origin = out.basis.xform(-origin);
		return out;
	}
};

inline SimTransform operator*(const SimTransform& a, const SimTransform& b) {
	SimTransform out;
	out.basis = a.basis * b.basis;
	out.origin = a.xform(b.origin);
	return out;
}

struct SimPlane {
	SimVec3 normal;
	float d;

	SimPlane() : normal(), d(0.0f) {}
	SimPlane(const SimVec3& p_normal, float p_d) : normal(p_normal), d(p_d) {}
	SimPlane(const SimVec3& p_normal, const SimVec3& p_point) : normal(p_normal), d(p_normal.dot(p_point)) {}

	bool is_point_over(const SimVec3& p) const { return normal.dot(p) > d; }
	float distance_to(const SimVec3& p) const { return normal.dot(p) - d; }
	SimVec3 project(const SimVec3& p) const { return p - normal * distance_to(p); }
};

struct SimAABB {
	SimVec3 position;
	SimVec3 size;

	bool has_point(const SimVec3& p) const {
		return p.x >= position.x && p.y >= position.y && p.z >= position.z &&
			p.x <= position.x + size.x && p.y <= position.y + size.y && p.z <= position.z + size.z;
	}
	void expand_to(const SimVec3& p) {
		const float min_x = std::min(position.x, p.x);
		const float min_y = std::min(position.y, p.y);
		const float min_z = std::min(position.z, p.z);
		const float max_x = std::max(position.x + size.x, p.x);
		const float max_y = std::max(position.y + size.y, p.y);
		const float max_z = std::max(position.z + size.z, p.z);
		position = SimVec3(min_x, min_y, min_z);
		size = SimVec3(max_x - min_x, max_y - min_y, max_z - min_z);
	}
	void grow_by(float amount) {
		position -= SimVec3(amount, amount, amount);
		size += SimVec3(amount + amount, amount + amount, amount + amount);
	}
};

inline SimBasis sim_basis_rotated_y(float angle) {
	const float c = cosf(angle);
	const float s = sinf(angle);
	SimBasis b;
	b.c0 = SimVec3(c, 0.0f, -s);
	b.c1 = SimVec3(0.0f, 1.0f, 0.0f);
	b.c2 = SimVec3(s, 0.0f, c);
	return b;
}

static inline SimVec3 get_closest_point_to_segment(SimVec3 p_point, SimVec3 p_segment_start, SimVec3 p_segment_end) {
	SimVec3 p = p_point - p_segment_start;
	SimVec3 n = p_segment_end - p_segment_start;
	float l2 = n.length_squared();
	if (l2 < 1e-20f) {
		return p_segment_start;
	}
	float d = n.dot(p) / l2;
	if (d <= 0.0f) {
		return p_segment_start;
	}
	if (d >= 1.0f) {
		return p_segment_end;
	}
	return p_segment_start + n * d;
}
