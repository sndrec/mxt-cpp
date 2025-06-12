#pragma once

#include "mxt_core/curve.h"

class PhysicsCarProperties {
public:
	char name[64];
	float weight;
	float acceleration;
	float max_speed;
	float max_health;
	float health_recharge_rate;
	float boost_duration;
	float boost_energy;
	float boost_topspeed_mult;
	float boost_accel_mult;
	float dash_topspeed_mult;
	float dash_accel_mult;
	float boost_and_dash_topspeed_mult;
	float boost_and_dash_accel_mult;
	float turbo_add_boost;
	float turbo_add_dashplate;
	float turbo_depletion;
	float turbo_depletion_boost;
	float turbo_depletion_boost_dash;
	float turbo_depletion_percentage;
	float strafe_power;
	float strafe_accel;
	float strafe_turn_effect;
	float strafe_qt_laterality;
	float strafe_mts_laterality;
	float strafe_qt_mult;
	float strafe_mts_mult;
	float turn_strafe_effect;
	float steer_acceleration;
	float steer_speed_target;
	float steer_speed_target_drift;
	float steer_reaction;
	float steer_reaction_damp;
	float angle_drag;
	float grip;
	float drift_accel;
	float turn_accel;
	float drag;
	Curve* vel_redir;
	Curve* vel_redir_drift;
	Curve* vel_redir_quickturn;
	Curve* vel_redir_mts;
	Curve* vel_redir_mult_by_speed;
};