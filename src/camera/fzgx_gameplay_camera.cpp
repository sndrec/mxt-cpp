#include "camera/fzgx_gameplay_camera.h"

#include "godot_cpp/core/class_db.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

using namespace godot;

namespace {
enum {
	ZOOM_FIRST_PERSON = 0,
	ZOOM_CLOSE = 1,
	ZOOM_MEDIUM = 2,
	ZOOM_FAR = 3,
	ZOOM_RESTORE = 5,
	BEHAVIOR_NORMAL = 0,
	FZGX_MS_B1 = 0x00000001u,
	FZGX_MS_AIRBORNE = 0x00000002u,
	FZGX_MS_BOOSTING = 0x00000020u,
	FZGX_MS_JUSTPRESSEDBOOST = 0x00000040u,
	FZGX_MS_0HP = 0x00000080u,
	FZGX_MS_B9 = 0x00000100u,
	FZGX_MS_B23 = 0x00400000u,
};

struct FollowTripletPreset {
	float local_height;
	float local_distance;
	int16_t pitch_angle16;
	uint16_t reserved0;
};

struct FollowPresetTriplet {
	FollowTripletPreset triplets[3];
};

static const FollowPresetTriplet FOLLOW_PRESETS[] = {
	{{{1.5f, 0.0f, -212, 0u}, {1.5f, 0.0f, -212, 0u}, {1.5f, 0.0f, 6826, 0u}}},
	{{{2.94000006f, 7.19000006f, -2065, 0u}, {4.0f, 5.0f, -1536, 0u}, {3.24000001f, 8.92000008f, 2304, 0u}}},
	{{{7.5999999f, 12.3000002f, -3584, 0u}, {8.80000019f, 5.9000001f, -4352, 0u}, {4.0999999f, 9.0f, 4096, 0u}}},
	{{{13.0100002f, 14.6899996f, -4600, 0u}, {13.0100002f, 14.6899996f, -4600, 0u}, {8.5f, 21.0f, 4864, 0u}}},
	{{{1.5f, 6.0f, -802, 0u}, {1.79999995f, 4.30000019f, 102, 0u}, {2.29999995f, 7.0f, 4357, 0u}}},
	{{{6.0f, 10.0f, -2823, 0u}, {6.0f, 10.3000002f, -760, 0u}, {4.57000017f, 15.0699997f, 4357, 0u}}},
};

static float clamp_exact(float value, float min_value, float max_value)
{
	if (value < min_value) {
		return min_value;
	}
	if (value > max_value) {
		return max_value;
	}
	return value;
}

static int16_t clamp_s16(int16_t value, int16_t min_value, int16_t max_value)
{
	if (value < min_value) {
		return min_value;
	}
	if (value > max_value) {
		return max_value;
	}
	return value;
}

static bool finite_vec3(const Vector3 &value)
{
	return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

static Vector3 normalized_or(const Vector3 &value, const Vector3 &fallback)
{
	const real_t len_sq = value.length_squared();
	if (!(len_sq > 0.0000001f)) {
		return fallback;
	}
	return value / std::sqrt((float)len_sq);
}

static Vector3 basis_xform(const Basis &basis, const Vector3 &value)
{
	return basis.get_column(0) * value.x + basis.get_column(1) * value.y + basis.get_column(2) * value.z;
}

static Vector3 basis_xform_inv_orthonormal(const Basis &basis, const Vector3 &value)
{
	return Vector3(
		basis.get_column(0).dot(value),
		basis.get_column(1).dot(value),
		basis.get_column(2).dot(value));
}

static float angle16_to_rad(int16_t angle)
{
	return (float)angle * (6.28318530717958647692f / 65536.0f);
}

static Vector3 rotate_about_axis_radians(const Vector3 &value, const Vector3 &axis, float radians)
{
	const Vector3 n = normalized_or(axis, Vector3(1.0f, 0.0f, 0.0f));
	const float s = std::sin(radians);
	const float c = std::cos(radians);
	return value * c + n.cross(value) * s + n * (n.dot(value) * (1.0f - c));
}

static Basis rotate_basis_y_right(const Basis &basis, int16_t angle16)
{
	const float radians = angle16_to_rad(angle16);
	const float s = std::sin(radians);
	const float c = std::cos(radians);
	const Vector3 x = basis.get_column(0);
	const Vector3 y = basis.get_column(1);
	const Vector3 z = basis.get_column(2);
	Basis out;
	out.set_column(0, x * c + z * -s);
	out.set_column(1, y);
	out.set_column(2, x * s + z * c);
	return out;
}

static void vec_to_euler(const Vector3 &value, int16_t &pitch_out, int16_t &yaw_out)
{
	const float x = -value.x;
	const float y = value.y;
	const float z = -value.z;
	const float flat = std::sqrt((float)(x * x + z * z));
	const float pitch = std::atan2(y, flat);
	const float yaw = std::atan2(x, z);
	pitch_out = (int16_t)(pitch * (65536.0f / 6.28318530717958647692f));
	yaw_out = (int16_t)(yaw * (65536.0f / 6.28318530717958647692f));
}

static Vector3 euler_to_vector(float scale, int16_t pitch_angle16, int16_t yaw_angle16)
{
	const float pitch = angle16_to_rad(pitch_angle16);
	const float yaw = angle16_to_rad(yaw_angle16);
	return Vector3(
		-scale * std::sin(yaw) * std::cos(pitch),
		scale * std::sin(pitch),
		-scale * std::cos(yaw) * std::cos(pitch));
}

static Basis build_direction_basis(const Vector3 &up_hint, const Vector3 &direction, const Basis &fallback)
{
	const Vector3 basis_z = normalized_or(-direction, fallback.get_column(2));
	Vector3 right = up_hint.cross(basis_z);
	if (right.length_squared() <= 0.0000001f) {
		right = fallback.get_column(0);
	} else {
		right.normalize();
	}
	Vector3 up = basis_z.cross(right);
	if (up.length_squared() <= 0.0000001f) {
		up = fallback.get_column(1);
	} else {
		up.normalize();
	}
	Basis out;
	out.set_column(0, right);
	out.set_column(1, up);
	out.set_column(2, basis_z);
	return out;
}

static Transform3D build_camera_transform(const Vector3 &position, const Vector3 &interest, const Vector3 &up)
{
	Vector3 backward = position - interest;
	if (backward.length_squared() <= 0.0000001f) {
		return Transform3D(Basis(), position);
	}
	backward.normalize();
	Vector3 right = up.cross(backward);
	if (right.length_squared() <= 0.0000001f) {
		right = Vector3(1.0f, 0.0f, 0.0f);
	} else {
		right.normalize();
	}
	Vector3 corrected_up = backward.cross(right);
	if (corrected_up.length_squared() <= 0.0000001f) {
		corrected_up = Vector3(0.0f, 1.0f, 0.0f);
	} else {
		corrected_up.normalize();
	}
	Basis basis;
	basis.set_column(0, right);
	basis.set_column(1, corrected_up);
	basis.set_column(2, backward);
	return Transform3D(basis, position);
}

static uint16_t restore_frames_from_state(int restore_state, int restore_move_frames)
{
	if (restore_state == 0) {
		return 0u;
	}
	if (restore_state == 1) {
		return 150u;
	}
	if (restore_state == 2) {
		return static_cast<uint16_t>(std::max(2, 150 - restore_move_frames));
	}
	return 20u;
}
}

FzgxGameplayCamera::FzgxGameplayCamera() = default;
FzgxGameplayCamera::~FzgxGameplayCamera() = default;

void FzgxGameplayCamera::_bind_methods()
{
	ClassDB::bind_method(D_METHOD("reset"), &FzgxGameplayCamera::reset);
	ClassDB::bind_method(D_METHOD("set_zoom_mode", "zoom_mode"), &FzgxGameplayCamera::set_zoom_mode);
	ClassDB::bind_method(D_METHOD("get_zoom_mode"), &FzgxGameplayCamera::get_zoom_mode);
	ClassDB::bind_method(D_METHOD(
		"step", "position", "position_old", "basis_physical", "track_up", "track_pos",
		"height_above_track", "speed_kmh", "vehicle_pitch_delta_radians", "camera_reorienting", "camera_repositioning",
		"turn_reaction_effect", "machine_state", "state_2", "tilt_fl_state", "tilt_fr_state",
		"tilt_bl_state", "tilt_br_state", "restore_state", "restore_move_frames",
		"aspect_ratio", "view_up_pressed", "view_down_pressed"),
		&FzgxGameplayCamera::step);
	ClassDB::bind_method(D_METHOD("get_render_transform", "alpha"), &FzgxGameplayCamera::get_render_transform);
	ClassDB::bind_method(D_METHOD("get_render_fov", "alpha"), &FzgxGameplayCamera::get_render_fov);
}

Transform3D FzgxGameplayCamera::build_transform(const Vector3 &position, const Vector3 &interest, const Vector3 &up)
{
	return build_camera_transform(position, interest, up);
}

void FzgxGameplayCamera::reset()
{
	const uint8_t persistent_zoom_mode = camera.persistent_saved_zoom_mode;
	camera = Runtime();
	camera.persistent_saved_zoom_mode = persistent_zoom_mode;
	current_transform = Transform3D();
	previous_transform = Transform3D();
	older_transform = Transform3D();
	current_fov = 55.0f;
	previous_fov = 55.0f;
	has_view = false;
}

void FzgxGameplayCamera::set_zoom_mode(int zoom_mode)
{
	const int16_t clamped = static_cast<int16_t>(
		std::clamp(zoom_mode, static_cast<int>(ZOOM_FIRST_PERSON), static_cast<int>(ZOOM_FAR)));
	camera.persistent_saved_zoom_mode = static_cast<uint8_t>(clamped);
	camera.saved_zoom_mode = clamped;
	if (camera.initialized && camera.zoom_mode != ZOOM_RESTORE) {
		camera.zoom_mode = clamped;
	}
}

int FzgxGameplayCamera::get_zoom_mode() const
{
	return static_cast<int>(camera.persistent_saved_zoom_mode);
}

static FollowPresetTriplet load_follow_preset(int zoom_mode, float aspect_ratio, float camera_parameter)
{
	size_t index = (zoom_mode >= 0) ? (size_t)zoom_mode : (size_t)ZOOM_CLOSE;
	if (index >= (sizeof(FOLLOW_PRESETS) / sizeof(FOLLOW_PRESETS[0]))) {
		index = ZOOM_CLOSE;
	}
	FollowPresetTriplet preset = FOLLOW_PRESETS[index];
	if (aspect_ratio > 1.4f) {
		float widescreen_height_offset = 1.1f;
		if ((zoom_mode != ZOOM_MEDIUM) && (zoom_mode != ZOOM_FAR)) {
			widescreen_height_offset = (std::isfinite(camera_parameter) && camera_parameter > 0.0f) ? camera_parameter : 0.3f;
		}
		for (int i = 0; i < 3; ++i) {
			preset.triplets[i].local_height -= widescreen_height_offset;
		}
	}
	return preset;
}

static void recompute_follow_point(FzgxGameplayCamera::Runtime &cam, const Vector3 &machine_position)
{
	cam.position = machine_position + basis_xform(cam.follow_basis, cam.local_follow_offset);
	const float pitch = angle16_to_rad(cam.pitch_angle16);
	const float s = std::sin(pitch);
	const float c = std::cos(pitch);
	const Vector3 y = cam.follow_basis.get_column(1);
	const Vector3 z = cam.follow_basis.get_column(2);
	const Vector3 pitched_z = y * -s + z * c;
	cam.interest = cam.position + pitched_z * -10.0f;
}

Dictionary FzgxGameplayCamera::step(
	Vector3 position,
	Vector3 position_old,
	Transform3D basis_physical,
	Vector3 track_up,
	Vector3 track_pos,
	float height_above_track,
	float speed_kmh,
	float vehicle_pitch_delta_radians,
	float camera_reorienting,
	float camera_repositioning,
	float turn_reaction_effect,
	int machine_state,
	int state_2,
	int tilt_fl_state,
	int tilt_fr_state,
	int tilt_bl_state,
	int tilt_br_state,
	int restore_state,
	int restore_move_frames,
	float aspect_ratio,
	bool view_up_pressed,
	bool view_down_pressed)
{
	(void)track_pos;
	(void)state_2;
	(void)tilt_fr_state;
	(void)tilt_bl_state;
	(void)tilt_br_state;
	Dictionary result;
	result["status"] = 0;
	if (!(aspect_ratio > 0.0f)) {
		aspect_ratio = 4.0f / 3.0f;
	}
	camera.aspect_ratio = aspect_ratio;
	const float camera_parameter = (std::isfinite(camera.camera_parameter) && camera.camera_parameter > 0.0f) ? camera.camera_parameter : 0.3f;
	const uint16_t frames_until_restored = restore_frames_from_state(restore_state, restore_move_frames);

	if (!camera.initialized) {
		const uint8_t persistent_zoom_mode = camera.persistent_saved_zoom_mode;
		camera = Runtime();
		camera.persistent_saved_zoom_mode = persistent_zoom_mode;
		camera.initialized = true;
		camera.aspect_ratio = aspect_ratio;
		camera.zoom_mode = camera.persistent_saved_zoom_mode;
		camera.saved_zoom_mode = camera.persistent_saved_zoom_mode;
		const FollowPresetTriplet preset = load_follow_preset(camera.zoom_mode, camera.aspect_ratio, camera_parameter);
		camera.follow_basis = basis_physical.basis;
		camera.up = normalized_or(basis_physical.basis.get_column(1), Vector3(0.0f, 1.0f, 0.0f));
		camera.local_follow_offset = Vector3(0.0f, preset.triplets[0].local_height, preset.triplets[0].local_distance);
		camera.pitch_angle16 = preset.triplets[0].pitch_angle16;
		recompute_follow_point(camera, position);
		camera.previous_position = camera.position;
	}

	camera.previous_position = camera.position;
	if (frames_until_restored > 1u || ((uint32_t)machine_state & FZGX_MS_0HP) != 0u) {
		camera.zoom_mode = ZOOM_RESTORE;
	} else {
		if (frames_until_restored == 1u) {
			camera.zoom_mode = camera.saved_zoom_mode;
			camera.persistent_saved_zoom_mode = (uint8_t)camera.saved_zoom_mode;
		}
		int zoom_mode = camera.zoom_mode;
		if (zoom_mode < ZOOM_FIRST_PERSON || zoom_mode > ZOOM_FAR) {
			zoom_mode = camera.saved_zoom_mode;
		}
		if (zoom_mode < ZOOM_FIRST_PERSON || zoom_mode > ZOOM_FAR) {
			zoom_mode = ZOOM_CLOSE;
		}
		if (view_down_pressed) {
			zoom_mode += 1;
		}
		if (view_up_pressed) {
			zoom_mode -= 1;
		}
		zoom_mode = std::clamp(zoom_mode, (int)ZOOM_FIRST_PERSON, (int)ZOOM_FAR);
		camera.zoom_mode = (int16_t)zoom_mode;
		camera.saved_zoom_mode = (int16_t)zoom_mode;
		camera.persistent_saved_zoom_mode = (uint8_t)zoom_mode;
	}

	const FollowPresetTriplet preset = load_follow_preset(camera.zoom_mode, camera.aspect_ratio, camera_parameter);
	const float clamped_speed = std::isfinite(speed_kmh) ? clamp_exact(speed_kmh, 0.0f, 1500.0f) : 0.0f;
	const float speed_ratio = clamped_speed / 1500.0f;
	float target_perspective = 55.0f + 53.0f * speed_ratio * speed_ratio;
	float target_local_y = preset.triplets[0].local_height + speed_ratio * (preset.triplets[1].local_height - preset.triplets[0].local_height);
	float target_local_z = preset.triplets[0].local_distance + speed_ratio * (preset.triplets[1].local_distance - preset.triplets[0].local_distance);
	int16_t target_pitch = (int16_t)((float)preset.triplets[0].pitch_angle16 +
		speed_ratio * (float)((int32_t)preset.triplets[1].pitch_angle16 - (int32_t)preset.triplets[0].pitch_angle16));
	const Vector3 displacement = position - position_old;

	if (camera.zoom_mode == ZOOM_FAR) {
		target_local_y = preset.triplets[0].local_height;
		target_local_z = preset.triplets[0].local_distance;
		target_perspective = 55.0f;
		target_pitch = preset.triplets[0].pitch_angle16;
	}
	const bool slope_orbit_enabled =
		((uint32_t)machine_state & FZGX_MS_AIRBORNE) == 0u &&
		camera.zoom_mode != ZOOM_FIRST_PERSON &&
		std::isfinite(vehicle_pitch_delta_radians);
	const float slope_orbit_zoom_scale = camera.zoom_mode == ZOOM_CLOSE ? 0.5f : 1.0f;
	const float target_slope_orbit = slope_orbit_enabled
		? vehicle_pitch_delta_radians * 12.75f * slope_orbit_zoom_scale
		: 0.0f;
	camera.slope_signal_radians += 0.06875f * (target_slope_orbit - camera.slope_signal_radians);
	camera.slope_orbit_radians += 0.10625f * (camera.slope_signal_radians - camera.slope_orbit_radians);

	const uint32_t machine_state_bits = (uint32_t)machine_state;
	const bool boost_active = (machine_state_bits & FZGX_MS_BOOSTING) != 0u;
	const bool boost_started =
		(machine_state_bits & (FZGX_MS_JUSTPRESSEDBOOST | FZGX_MS_B23)) != 0u ||
		(boost_active && !camera.boost_was_active);
	float perspective_step = clamp_exact(0.05f - 0.00004f * clamped_speed, 0.01f, 0.05f);
	if (!boost_started) {
		if (!boost_active || camera.zoom_mode == ZOOM_FAR) {
			if (camera.perspective_transition_counter > 0) {
				camera.perspective_transition_counter -= 1;
			}
			if (camera.perspective_transition_counter < 11) {
				camera.perspective += perspective_step * (target_perspective - camera.perspective);
			} else if (clamped_speed <= camera.previous_speed_kmh) {
				camera.perspective += 0.01f * (target_perspective - camera.perspective);
			} else {
				camera.perspective += 0.2f * (camera.boost_perspective_target - camera.perspective);
			}
			camera.previous_speed_kmh = clamped_speed;
		} else {
			if (camera.perspective_transition_counter < 20) {
				camera.perspective_transition_counter += 1;
			}
			if (camera.perspective_transition_counter < 11) {
				camera.perspective += ((camera.zoom_mode == ZOOM_MEDIUM) ? 0.05f : 0.1f) * (80.0f - camera.perspective);
			} else if (clamped_speed <= camera.previous_speed_kmh) {
				camera.perspective += 0.01f * (target_perspective - camera.perspective);
			} else {
				camera.perspective += 0.2f * (camera.boost_perspective_target - camera.perspective);
			}
			camera.previous_speed_kmh = clamped_speed;
		}
	} else {
		camera.boost_perspective_target = clamp_exact(camera.perspective * 1.35f, 80.0f, 108.0f);
	}
	camera.boost_was_active = boost_active;

	camera.local_follow_offset.y += 0.15f * (target_local_y - camera.local_follow_offset.y);
	camera.local_follow_offset.z += 0.15f * (target_local_z - camera.local_follow_offset.z);
	camera.pitch_angle16 = (int16_t)((float)camera.pitch_angle16 + 0.15f * (float)((int32_t)target_pitch - (int32_t)camera.pitch_angle16));

	Basis target_basis = basis_physical.basis;
	const bool any_camera_corner_flag =
		((tilt_fl_state | tilt_fr_state | tilt_bl_state | tilt_br_state) & 4) != 0;
	if (!any_camera_corner_flag || camera.zoom_mode == ZOOM_FIRST_PERSON || (((uint32_t)machine_state & FZGX_MS_B9) != 0u)) {
		if ((((uint32_t)machine_state & FZGX_MS_AIRBORNE) != 0u) && camera.zoom_mode != ZOOM_FIRST_PERSON && height_above_track == 0.0f) {
			const Vector3 local_displacement = basis_xform_inv_orthonormal(basis_physical.basis, displacement);
			int16_t clamped_pitch = 0;
			int16_t clamped_yaw = 0;
			vec_to_euler(local_displacement, clamped_pitch, clamped_yaw);
			clamped_pitch = clamp_s16(clamped_pitch, (int16_t)-0x0500, (int16_t)-0x0100);
			clamped_yaw = clamp_s16(clamped_yaw, (int16_t)-100, (int16_t)100);
			const Vector3 rotated_world_direction = basis_xform(basis_physical.basis, euler_to_vector(1.0f, clamped_pitch, clamped_yaw));
			target_basis = build_direction_basis(camera.up, rotated_world_direction, basis_physical.basis);
		}
	} else {
		target_basis = build_direction_basis(camera.up, displacement, basis_physical.basis);
	}
	const int turn_reaction_step = (int)(0.4f * turn_reaction_effect);
	target_basis = rotate_basis_y_right(target_basis, (int16_t)(182.04445f * (float)turn_reaction_step));

	float reorient_scale = clamp_exact(clamped_speed / 5000.0f, 0.05f, 0.12f) * camera_reorienting;
	if (camera.zoom_mode == ZOOM_MEDIUM) {
		reorient_scale *= 1.2f;
	}
	if (camera.zoom_mode == ZOOM_FAR) {
		reorient_scale *= 1.4f;
	}
	if (camera.zoom_mode == ZOOM_FIRST_PERSON) {
		reorient_scale = 0.2f;
	}
	camera.follow_basis = camera.follow_basis.slerp(target_basis, reorient_scale).orthonormalized();

	recompute_follow_point(camera, position);

	const Vector3 target_up = normalized_or(basis_physical.basis.get_column(1), normalized_or(track_up, Vector3(0.0f, 1.0f, 0.0f)));
	float reposition_scale = ((((uint32_t)machine_state & FZGX_MS_AIRBORNE) != 0u) ? camera_parameter : 0.1f) * camera_repositioning;
	if (camera.zoom_mode == ZOOM_MEDIUM) {
		reposition_scale *= 1.2f;
	}
	if (camera.zoom_mode == ZOOM_FAR) {
		reposition_scale *= 1.7f;
	}
	if (camera.zoom_mode == ZOOM_FIRST_PERSON) {
		reposition_scale = 0.2f;
	}
	camera.up = normalized_or(camera.up.lerp(target_up, reposition_scale), target_up);

	if (camera.zoom_mode == ZOOM_CLOSE || camera.zoom_mode == ZOOM_MEDIUM) {
		if (((uint32_t)machine_state & FZGX_MS_AIRBORNE) == 0u) {
			camera.airborne_transition_counter = 0;
		} else if (camera.airborne_transition_counter < 5) {
			camera.airborne_transition_counter += 1;
		}
		if (camera.airborne_transition_counter <= 1) {
			camera.vertical_offset_state += 0.05f * -camera.vertical_offset_state;
			camera.interest_vertical_offset_state += 0.05f * -camera.interest_vertical_offset_state;
		} else {
			if (camera.zoom_mode == ZOOM_CLOSE) {
				float offset_blend = clamp_exact(0.2f - 0.04f * (3.0f - camera.vertical_offset_state), 0.08f, 0.2f);
				camera.vertical_offset_state += offset_blend * (1.5f - camera.vertical_offset_state);
				offset_blend = clamp_exact(0.2f - 0.04f * (3.0f - camera.interest_vertical_offset_state), 0.08f, 0.2f);
				camera.interest_vertical_offset_state += offset_blend * (1.5f - camera.interest_vertical_offset_state);
			} else {
				camera.vertical_offset_state += 0.2f * (2.3f - camera.vertical_offset_state);
				camera.interest_vertical_offset_state += 0.2f * (2.3f - camera.interest_vertical_offset_state);
			}
		}
		camera.position = position + basis_xform(camera.follow_basis, Vector3(
			camera.local_follow_offset.x,
			camera.local_follow_offset.y - camera.vertical_offset_state,
			camera.local_follow_offset.z));
		Vector3 local_interest = basis_xform_inv_orthonormal(camera.follow_basis, camera.interest - position);
		local_interest.y -= camera.interest_vertical_offset_state * 0.7f;
		camera.interest = position + basis_xform(camera.follow_basis, local_interest);
	}

	if (camera.zoom_mode != ZOOM_FIRST_PERSON && std::abs(camera.slope_orbit_radians) > 0.00001f) {
		const Vector3 orbit_axis = normalized_or(
			camera.follow_basis.get_column(0),
			basis_physical.basis.get_column(0));
		camera.position = camera.interest + rotate_about_axis_radians(
			camera.position - camera.interest,
			orbit_axis,
			camera.slope_orbit_radians);
	}

	if (!finite_vec3(camera.position) || !finite_vec3(camera.interest) || !finite_vec3(camera.up)) {
		camera.position = position;
		camera.previous_position = position;
		camera.interest = position - basis_physical.basis.get_column(2) * 5.0f;
		camera.up = normalized_or(basis_physical.basis.get_column(1), Vector3(0.0f, 1.0f, 0.0f));
		camera.perspective = 55.0f;
	}

	const Transform3D initial_transform = build_camera_transform(camera.previous_position, camera.interest, camera.up);
	older_transform = has_view ? previous_transform : initial_transform;
	previous_transform = has_view ? current_transform : initial_transform;
	previous_fov = has_view ? current_fov : camera.perspective;
	current_transform = build_camera_transform(camera.position, camera.interest, normalized_or(camera.up, Vector3(0.0f, 1.0f, 0.0f)));
	current_fov = camera.perspective;
	has_view = true;

	result["transform"] = current_transform;
	result["fov"] = current_fov;
	result["zoom_mode"] = static_cast<int>(camera.zoom_mode);
	result["behavior_state"] = static_cast<int>(BEHAVIOR_NORMAL);
	return result;
}

Transform3D FzgxGameplayCamera::get_render_transform(float alpha) const
{
	if (!has_view) {
		return current_transform;
	}
	const float clamped = std::clamp(alpha, 0.0f, 1.0f);
	return previous_transform.interpolate_with(current_transform, clamped);
}

Transform3D FzgxGameplayCamera::get_previous_render_transform(float alpha) const
{
	if (!has_view) {
		return previous_transform;
	}
	const float clamped = std::clamp(alpha, 0.0f, 1.0f);
	return older_transform.interpolate_with(previous_transform, clamped);
}

float FzgxGameplayCamera::get_render_fov(float alpha) const
{
	if (!has_view) {
		return current_fov;
	}
	const float clamped = std::clamp(alpha, 0.0f, 1.0f);
	return previous_fov + (current_fov - previous_fov) * clamped;
}
