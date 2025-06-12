#pragma once

#include "godot_cpp/classes/input.hpp"
//B:\programming\raylib - game - template - main\projects\gdextension_mxto_sim\godot - cpp\gen\include\godot_cpp\classes\input.hpp

class PlayerInput 
{
public:
	float strafe_left = 0.0f;
	float strafe_right = 0.0f;
	float steer_horizontal = 0.0f;
	float steer_vertical = 0.0f;
	float accelerate = 0.0f;
	float brake = 0.0f;
	bool spinattack = false;
	bool boost = false;

	static PlayerInput from_neutral()
	{
		PlayerInput new_input{};
		return new_input;
	}

	static PlayerInput from_player_input()
	{
		PlayerInput new_input{};
		char* str1 = "StrafeLeft";
		char* str2 = "StrafeRight";
		char* str3 = "SteerUp";
		char* str4 = "SteerDown";
		char* str5 = "SteerLeft";
		char* str6 = "SteerRight";
		char* str7 = "Accelerate";
		char* str8 = "Brake";
		char* str9 = "SpinAttack";
		char* str10 = "Boost";
		godot::StringName string_strafe_left = godot::StringName(str1);
		godot::StringName string_strafe_right = godot::StringName(str2);
		godot::StringName string_steer_up = godot::StringName(str3);
		godot::StringName string_steer_dn = godot::StringName(str4);
		godot::StringName string_steer_lt = godot::StringName(str5);
		godot::StringName string_steer_rt = godot::StringName(str6);
		godot::StringName string_accel = godot::StringName(str7);
		godot::StringName string_brake = godot::StringName(str8);
		godot::StringName string_spinattack = godot::StringName(str9);
		godot::StringName string_boost = godot::StringName(str10);

		godot::Input* input_singleton = godot::Input::get_singleton();

		new_input.strafe_left = input_singleton->get_action_strength(string_strafe_left);
		new_input.strafe_right = input_singleton->get_action_strength(string_strafe_right);
		new_input.steer_horizontal = input_singleton->get_axis(string_steer_lt, string_steer_rt);
		new_input.steer_vertical = input_singleton->get_axis(string_steer_up, string_steer_dn);
		new_input.accelerate = input_singleton->get_action_strength(string_accel);
		new_input.brake = input_singleton->get_action_strength(string_brake);
		new_input.spinattack = input_singleton->is_action_just_pressed(string_spinattack);
		new_input.boost = input_singleton->is_action_just_pressed(string_boost);

		return new_input;
	}
};