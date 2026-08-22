#ifndef MXT_FZGX_GAMEPLAY_CAMERA_H
#define MXT_FZGX_GAMEPLAY_CAMERA_H

#include "godot_cpp/classes/ref_counted.hpp"
#include "godot_cpp/variant/dictionary.hpp"
#include "godot_cpp/variant/transform3d.hpp"
#include "godot_cpp/variant/vector3.hpp"

namespace godot {

class FzgxGameplayCamera : public RefCounted {
	GDCLASS(FzgxGameplayCamera, RefCounted)

public:
	struct Runtime {
		bool initialized = false;
		uint8_t persistent_saved_zoom_mode = 1;
		int16_t behavior_state = 0;
		int16_t zoom_mode = 1;
		int16_t saved_zoom_mode = 1;
		int16_t pitch_angle16 = 0;
		int16_t airborne_transition_counter = 0;
		int32_t perspective_transition_counter = 0;
		float aspect_ratio = 4.0f / 3.0f;
		float camera_parameter = -1.0f;
		float perspective = 55.0f;
		float clearance_ratio = 0.0f;
		float boost_perspective_target = 0.0f;
		bool boost_was_active = false;
		float vertical_offset_state = 0.0f;
		float interest_vertical_offset_state = 0.0f;
		float slope_signal_radians = 0.0f;
		float slope_orbit_radians = 0.0f;
		float previous_speed_kmh = 0.0f;
		Vector3 local_follow_offset = Vector3();
		Basis follow_basis = Basis();
		Vector3 position = Vector3();
		Vector3 previous_position = Vector3();
		Vector3 interest = Vector3();
		Vector3 up = Vector3(0.0f, 1.0f, 0.0f);
	};

private:
	Runtime camera;
	Transform3D current_transform = Transform3D();
	Transform3D previous_transform = Transform3D();
	Transform3D older_transform = Transform3D();
	float current_fov = 55.0f;
	float previous_fov = 55.0f;
	bool has_view = false;

	static Transform3D build_transform(const Vector3 &position, const Vector3 &interest, const Vector3 &up);

protected:
	static void _bind_methods();

public:
	FzgxGameplayCamera();
	~FzgxGameplayCamera();

	void reset();
	void set_zoom_mode(int zoom_mode);
	int get_zoom_mode() const;
	Dictionary step(
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
		bool view_down_pressed);
	Transform3D get_render_transform(float alpha) const;
	Transform3D get_previous_render_transform(float alpha) const;
	float get_render_fov(float alpha) const;
};

}

#endif
