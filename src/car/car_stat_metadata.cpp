#include "car/car_stat_metadata.h"

namespace {

static constexpr CarStatMetadata CAR_STAT_METADATA[CAR_STAT_COUNT] = {
	{"weight_kg", "Weight",
	 "Machine mass. Weight changes acceleration, handling forces, collisions, "
	 "suspension, and airborne behavior.",
	 "kg", "Machine", CAR_STAT_ACTIVITY_GAMEPLAY, false, CAR_STAT_DIRECTION_NONE,
	 CAR_STAT_DIRECTION_NONE},
	{"acceleration", "Acceleration",
	 "Primary engine response used to approach the current drive target speed.", "scalar", "Drive",
	 CAR_STAT_ACTIVITY_GAMEPLAY, true, CAR_STAT_DIRECTION_HIGHER_BENEFIT,
	 CAR_STAT_DIRECTION_HIGHER_BENEFIT},
	{"max_speed", "Top Speed", "Extends the speed range in the drive-speed response calculation.",
	 "scalar", "Drive", CAR_STAT_ACTIVITY_GAMEPLAY, true, CAR_STAT_DIRECTION_HIGHER_BENEFIT,
	 CAR_STAT_DIRECTION_HIGHER_BENEFIT},
	{"grip_1", "Breakaway Resistance",
	 "Lateral-slip threshold used before a machine begins drifting. Higher "
	 "values resist breakaway longer.",
	 "scalar", "Handling", CAR_STAT_ACTIVITY_GAMEPLAY, true, CAR_STAT_DIRECTION_HIGHER_BENEFIT,
	 CAR_STAT_DIRECTION_CONTEXT_DEPENDENT},
	{"grip_2", "Drift Rotation Strength",
	 "Scales the angular correction applied while a tilt point is drifting. "
	 "Higher values make drift forces rotate the machine's facing direction more strongly.",
	 "scalar", "Handling", CAR_STAT_ACTIVITY_GAMEPLAY, false, CAR_STAT_DIRECTION_CONTEXT_DEPENDENT,
	 CAR_STAT_DIRECTION_CONTEXT_DEPENDENT},
	{"grip_3", "Re-grip Tendency",
	 "Lateral-slip threshold used while drifting and when deciding to re-grip. "
	 "Higher values favor a planted machine.",
	 "scalar", "Handling", CAR_STAT_ACTIVITY_GAMEPLAY, true, CAR_STAT_DIRECTION_HIGHER_BENEFIT,
	 CAR_STAT_DIRECTION_CONTEXT_DEPENDENT},
	{"turn_tension", "Cornering Tension",
	 "Scales lateral restoring force at the machine's tilt points. Terrain and "
	 "drift state also affect the result.",
	 "scalar", "Handling", CAR_STAT_ACTIVITY_GAMEPLAY, false, CAR_STAT_DIRECTION_CONTEXT_DEPENDENT,
	 CAR_STAT_DIRECTION_CONTEXT_DEPENDENT},
	{"drift_accel", "Drift Acceleration",
	 "Adds forward drive while the machine carries lateral slip. More is not "
	 "unconditionally better because it changes drift behavior.",
	 "scalar", "Handling", CAR_STAT_ACTIVITY_GAMEPLAY, false, CAR_STAT_DIRECTION_CONTEXT_DEPENDENT,
	 CAR_STAT_DIRECTION_CONTEXT_DEPENDENT},
	{"turn_movement", "Steering Strength", "Base yaw impulse produced by steering input.", "scalar",
	 "Handling", CAR_STAT_ACTIVITY_GAMEPLAY, false, CAR_STAT_DIRECTION_CONTEXT_DEPENDENT,
	 CAR_STAT_DIRECTION_CONTEXT_DEPENDENT},
	{"strafe_turn", "Strafe Steering",
	 "Additional steering strength produced by combined strafe and steering "
	 "input.",
	 "scalar", "Handling", CAR_STAT_ACTIVITY_GAMEPLAY, false, CAR_STAT_DIRECTION_CONTEXT_DEPENDENT,
	 CAR_STAT_DIRECTION_CONTEXT_DEPENDENT},
	{"strafe", "Strafe Angle", "Changes the physical steering basis in proportion to strafe input.",
	 "degrees", "Handling", CAR_STAT_ACTIVITY_GAMEPLAY, false, CAR_STAT_DIRECTION_CONTEXT_DEPENDENT,
	 CAR_STAT_DIRECTION_CONTEXT_DEPENDENT},
	{"turn_reaction", "Steering Angle",
	 "Changes the physical steering basis in proportion to steering input.", "degrees", "Handling",
	 CAR_STAT_ACTIVITY_GAMEPLAY, false, CAR_STAT_DIRECTION_CONTEXT_DEPENDENT,
	 CAR_STAT_DIRECTION_CONTEXT_DEPENDENT},
	{"turn_decel", "Lateral Speed Response",
	 "Couples lateral speed into the drive-speed response while cornering. Its "
	 "benefit depends on the surrounding handling values.",
	 "scalar", "Handling", CAR_STAT_ACTIVITY_GAMEPLAY, false, CAR_STAT_DIRECTION_CONTEXT_DEPENDENT,
	 CAR_STAT_DIRECTION_CONTEXT_DEPENDENT},
	{"drag", "Drag",
	 "Constant base-speed loss applied every simulation tick, separate from the automatic "
	 "speed-squared air resistance shared by every machine.", "speed/tick",
	 "Drive", CAR_STAT_ACTIVITY_GAMEPLAY, true, CAR_STAT_DIRECTION_LOWER_BENEFIT,
	 CAR_STAT_DIRECTION_LOWER_BENEFIT},
	{"body", "Damage Multiplier",
	 "Multiplies collision and breakdown damage. Lower values make the machine "
	 "more durable.",
	 "multiplier", "Machine", CAR_STAT_ACTIVITY_GAMEPLAY, true, CAR_STAT_DIRECTION_LOWER_BENEFIT,
	 CAR_STAT_DIRECTION_LOWER_BENEFIT},
	{"camera_reorienting", "Camera Reorientation",
	 "Scales how quickly the race camera reorients around this machine.", "multiplier", "Camera",
	 CAR_STAT_ACTIVITY_PRESENTATION, false, CAR_STAT_DIRECTION_NONE, CAR_STAT_DIRECTION_NONE},
	{"camera_repositioning", "Camera Repositioning",
	 "Scales how quickly the race camera follows this machine's position.", "multiplier", "Camera",
	 CAR_STAT_ACTIVITY_PRESENTATION, false, CAR_STAT_DIRECTION_NONE, CAR_STAT_DIRECTION_NONE},
	{"track_collision", "Track Collision Radius (Inactive)",
	 "Copied into runtime state but not currently read by collision code. "
	 "Collision geometry comes from authored corners.",
	 "meters", "Technical", CAR_STAT_ACTIVITY_ASSIGNED_UNUSED, false, CAR_STAT_DIRECTION_NONE,
	 CAR_STAT_DIRECTION_NONE},
	{"obstacle_collision", "Obstacle Collision Radius (Inactive)",
	 "Copied into runtime state but not currently read by collision code. "
	 "Collision geometry comes from authored corners.",
	 "meters", "Technical", CAR_STAT_ACTIVITY_ASSIGNED_UNUSED, false, CAR_STAT_DIRECTION_NONE,
	 CAR_STAT_DIRECTION_NONE},
	{"max_energy", "Maximum Energy", "Maximum energy available for damage and manual boosting.",
	 "energy", "Machine", CAR_STAT_ACTIVITY_GAMEPLAY, true, CAR_STAT_DIRECTION_HIGHER_BENEFIT,
	 CAR_STAT_DIRECTION_HIGHER_BENEFIT},
	{"boost_energy_use_rate", "Boost Energy Use",
	 "Multiplies energy consumed on every manual-boost tick.", "multiplier", "Boost",
	 CAR_STAT_ACTIVITY_GAMEPLAY, true, CAR_STAT_DIRECTION_LOWER_BENEFIT,
	 CAR_STAT_DIRECTION_LOWER_BENEFIT},
	{"energy_recharge_rate", "Energy Recharge", "Multiplies energy restored by recharge surfaces.",
	 "multiplier", "Boost", CAR_STAT_ACTIVITY_GAMEPLAY, true, CAR_STAT_DIRECTION_HIGHER_BENEFIT,
	 CAR_STAT_DIRECTION_HIGHER_BENEFIT},
	{"accel_press_grip_frames", "Accelerator Grip Frames",
	 "Frames of forced restoring grip granted by the accelerator-press "
	 "handling state.",
	 "frames", "Handling", CAR_STAT_ACTIVITY_GAMEPLAY, true, CAR_STAT_DIRECTION_HIGHER_BENEFIT,
	 CAR_STAT_DIRECTION_CONTEXT_DEPENDENT},
	{"manual_turbo_gain", "Manual Boost Turbo", "Turbo added when a manual boost starts.", "turbo",
	 "Boost", CAR_STAT_ACTIVITY_GAMEPLAY, true, CAR_STAT_DIRECTION_HIGHER_BENEFIT,
	 CAR_STAT_DIRECTION_HIGHER_BENEFIT},
	{"dashplate_turbo_gain", "Dashplate Turbo", "Turbo added when a dashplate boost starts.",
	 "turbo", "Boost", CAR_STAT_ACTIVITY_GAMEPLAY, true, CAR_STAT_DIRECTION_HIGHER_BENEFIT,
	 CAR_STAT_DIRECTION_HIGHER_BENEFIT},
	{"jumpplate_turbo_gain", "Jumpplate Turbo", "Turbo added when leaving a jumpplate.", "turbo",
	 "Boost", CAR_STAT_ACTIVITY_GAMEPLAY, true, CAR_STAT_DIRECTION_HIGHER_BENEFIT,
	 CAR_STAT_DIRECTION_HIGHER_BENEFIT},
	{"dashplate_turbo_heat_multiplier", "Dashplate Heat Reward",
	 "Scales the extra turbo awarded by accumulated dashplate heat.", "multiplier", "Boost",
	 CAR_STAT_ACTIVITY_GAMEPLAY, true, CAR_STAT_DIRECTION_HIGHER_BENEFIT,
	 CAR_STAT_DIRECTION_HIGHER_BENEFIT},
	{"turbo_flat_loss_per_second", "Flat Turbo Loss",
	 "Flat turbo removed per second while turbo is being updated.", "turbo/second", "Boost",
	 CAR_STAT_ACTIVITY_GAMEPLAY, true, CAR_STAT_DIRECTION_LOWER_BENEFIT,
	 CAR_STAT_DIRECTION_LOWER_BENEFIT},
	{"turbo_percent_loss_per_second", "Proportional Turbo Loss",
	 "Additional turbo removed per second as a proportion of current turbo.", "ratio/second",
	 "Boost", CAR_STAT_ACTIVITY_GAMEPLAY, true, CAR_STAT_DIRECTION_LOWER_BENEFIT,
	 CAR_STAT_DIRECTION_LOWER_BENEFIT},
	{"turbo_top_speed_effect", "Turbo Speed Effect",
	 "Controls how accumulated turbo changes the drive-speed response "
	 "denominator.",
	 "scalar", "Boost", CAR_STAT_ACTIVITY_GAMEPLAY, false, CAR_STAT_DIRECTION_CONTEXT_DEPENDENT,
	 CAR_STAT_DIRECTION_CONTEXT_DEPENDENT},
	{"manual_boost_duration_seconds", "Manual Boost Duration",
	 "Duration of a newly started manual boost.", "seconds", "Boost", CAR_STAT_ACTIVITY_GAMEPLAY,
	 true, CAR_STAT_DIRECTION_HIGHER_BENEFIT, CAR_STAT_DIRECTION_HIGHER_BENEFIT},
	{"dashplate_boost_duration_seconds", "Dashplate Boost Duration",
	 "Duration granted by a dashplate hit.", "seconds", "Boost", CAR_STAT_ACTIVITY_GAMEPLAY, true,
	 CAR_STAT_DIRECTION_HIGHER_BENEFIT, CAR_STAT_DIRECTION_HIGHER_BENEFIT},
	{"s_boost_base_speed_add_per_second", "S-Boost Speed Gain",
	 "Flat base speed added each second while S-Boost is active.", "speed/second", "Boost",
	 CAR_STAT_ACTIVITY_GAMEPLAY, true, CAR_STAT_DIRECTION_HIGHER_BENEFIT,
	 CAR_STAT_DIRECTION_HIGHER_BENEFIT},
	{"shift_boost_base_speed_add", "Shift Boost Speed Add",
	 "Flat base speed added when a successful shift boost triggers.", "speed", "Boost",
	 CAR_STAT_ACTIVITY_GAMEPLAY, true, CAR_STAT_DIRECTION_HIGHER_BENEFIT,
	 CAR_STAT_DIRECTION_HIGHER_BENEFIT},
	{"shift_boost_velocity_multiplier", "Shift Boost Velocity",
	 "Multiplies world velocity when a successful shift boost triggers.", "multiplier", "Boost",
	 CAR_STAT_ACTIVITY_GAMEPLAY, true, CAR_STAT_DIRECTION_HIGHER_BENEFIT,
	 CAR_STAT_DIRECTION_HIGHER_BENEFIT},
	{"air_pitch_up_speed_loss_factor", "Pitch-Up Air Drag",
	 "Speed loss caused by pitching upward while airborne.", "factor", "Air",
	 CAR_STAT_ACTIVITY_GAMEPLAY, true, CAR_STAT_DIRECTION_LOWER_BENEFIT,
	 CAR_STAT_DIRECTION_LOWER_BENEFIT},
	{"air_glide_steering_speed_loss_factor", "Glide Steering Air Drag",
	 "Speed loss caused by steering during low-gravity flight without a nearby road surface.",
	 "factor", "Air",
	 CAR_STAT_ACTIVITY_GAMEPLAY, true, CAR_STAT_DIRECTION_LOWER_BENEFIT,
	 CAR_STAT_DIRECTION_LOWER_BENEFIT},
	{"drive_target_speed_multiplier", "Drive Target Speed",
	 "Multiplies the drive target-speed component, primarily for boost states.", "multiplier",
	 "Drive", CAR_STAT_ACTIVITY_GAMEPLAY, true, CAR_STAT_DIRECTION_HIGHER_BENEFIT,
	 CAR_STAT_DIRECTION_HIGHER_BENEFIT},
	{"acceleration_response_multiplier", "Acceleration Response",
	 "Multiplies convergence strength toward the current drive target. It "
	 "affects acceleration and deceleration.",
	 "multiplier", "Drive", CAR_STAT_ACTIVITY_GAMEPLAY, false, CAR_STAT_DIRECTION_CONTEXT_DEPENDENT,
	 CAR_STAT_DIRECTION_CONTEXT_DEPENDENT},
	{"forward_thrust_multiplier", "Forward Thrust",
	 "Multiplies the final physical forward thrust emitted by the drive "
	 "calculation.",
	 "multiplier", "Drive", CAR_STAT_ACTIVITY_GAMEPLAY, false, CAR_STAT_DIRECTION_CONTEXT_DEPENDENT,
	 CAR_STAT_DIRECTION_CONTEXT_DEPENDENT},
	{"drift_turn_movement", "Drift Steering Strength",
	 "Base yaw impulse produced by steering input while the machine is drifting.", "scalar",
	 "Handling", CAR_STAT_ACTIVITY_GAMEPLAY, false, CAR_STAT_DIRECTION_CONTEXT_DEPENDENT,
	 CAR_STAT_DIRECTION_CONTEXT_DEPENDENT},
	{"max_turn_rate", "Maximum Turning Rate",
	 "Limits how quickly the machine can rotate around its vertical axis.", "degrees/second",
	 "Handling", CAR_STAT_ACTIVITY_GAMEPLAY, true, CAR_STAT_DIRECTION_HIGHER_BENEFIT,
	 CAR_STAT_DIRECTION_CONTEXT_DEPENDENT}};

static constexpr CarStatMetadata UNKNOWN_STAT_METADATA = {"unknown",
														  "Unknown",
														  "Unknown car property.",
														  "scalar",
														  "Technical",
														  CAR_STAT_ACTIVITY_ASSIGNED_UNUSED,
														  false,
														  CAR_STAT_DIRECTION_NONE,
														  CAR_STAT_DIRECTION_NONE};

static_assert(sizeof(CAR_STAT_METADATA) / sizeof(CAR_STAT_METADATA[0]) == CAR_STAT_COUNT);

} // namespace

const CarStatMetadata &get_car_stat_metadata(CarStatId stat) {
	return stat < CAR_STAT_COUNT ? CAR_STAT_METADATA[stat] : UNKNOWN_STAT_METADATA;
}

const char *car_stat_activity_name(CarStatActivity activity) {
	switch (activity) {
	case CAR_STAT_ACTIVITY_GAMEPLAY:
		return "gameplay";
	case CAR_STAT_ACTIVITY_PRESENTATION:
		return "presentation";
	case CAR_STAT_ACTIVITY_ASSIGNED_UNUSED:
		return "assigned_unused";
	}
	return "unknown";
}

const char *car_stat_direction_name(CarStatDirection direction) {
	switch (direction) {
	case CAR_STAT_DIRECTION_NONE:
		return "none";
	case CAR_STAT_DIRECTION_HIGHER_BENEFIT:
		return "higher_benefit";
	case CAR_STAT_DIRECTION_LOWER_BENEFIT:
		return "lower_benefit";
	case CAR_STAT_DIRECTION_CONTEXT_DEPENDENT:
		return "context_dependent";
	}
	return "unknown";
}
