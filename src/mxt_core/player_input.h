#pragma once

#include "godot_cpp/classes/input.hpp"
#include "godot_cpp/variant/dictionary.hpp"
#include "godot_cpp/variant/packed_byte_array.hpp"

class PlayerInput
{
public:
        static constexpr uint8_t RAW_BIT_PRECISION = 254;
        static constexpr uint8_t AXIS_NEUTRAL = RAW_BIT_PRECISION / 2;
        static constexpr uint8_t TRIGGER_NEUTRAL = 0;
	float strafe_left = 0.0f;
	float strafe_right = 0.0f;
	float steer_horizontal = 0.0f;
	float steer_vertical = 0.0f;
	float accelerate = 0.0f;
	float brake = 0.0f;
	bool spinattack = false;
        bool sideattack = false;
	bool boost = false;

	static PlayerInput from_neutral()
	{
		PlayerInput new_input{};
		return new_input;
	}

        static PlayerInput from_dict(const godot::Dictionary &dict)
        {
                PlayerInput new_input{};
                if (dict.has("strafe_left"))
                        new_input.strafe_left = godot::Variant(dict["strafe_left"]).operator float();
                if (dict.has("strafe_right"))
                        new_input.strafe_right = godot::Variant(dict["strafe_right"]).operator float();
                if (dict.has("steer_horizontal"))
                        new_input.steer_horizontal = godot::Variant(dict["steer_horizontal"]).operator float();
                if (dict.has("steer_vertical"))
                        new_input.steer_vertical = godot::Variant(dict["steer_vertical"]).operator float();
                if (dict.has("accelerate"))
                        new_input.accelerate = godot::Variant(dict["accelerate"]).operator float();
                if (dict.has("brake"))
                        new_input.brake = godot::Variant(dict["brake"]).operator float();
                if (dict.has("spinattack"))
                        new_input.spinattack = godot::Variant(dict["spinattack"]).operator bool();
                if (dict.has("sideattack"))
                        new_input.sideattack = godot::Variant(dict["sideattack"]).operator bool();
                if (dict.has("boost"))
                        new_input.boost = godot::Variant(dict["boost"]).operator bool();
                return new_input;
        }

        static PlayerInput from_bytes(const godot::PackedByteArray &arr)
        {
                PlayerInput out{};
                const uint8_t *data = arr.ptr();
                int idx = 0;
                if (arr.size() == 0)
                        return out;
                uint8_t bitmask = data[idx++];
                if (bitmask & (1 << 0)) out.strafe_left = float(data[idx++]) / float(RAW_BIT_PRECISION);
                if (bitmask & (1 << 1)) out.strafe_right = float(data[idx++]) / float(RAW_BIT_PRECISION);
                if (bitmask & (1 << 2)) out.steer_horizontal = (float(data[idx++]) / float(RAW_BIT_PRECISION)) * 2.0f - 1.0f;
                if (bitmask & (1 << 3)) out.steer_vertical = (float(data[idx++]) / float(RAW_BIT_PRECISION)) * 2.0f - 1.0f;
                if (bitmask & (1 << 4)) out.accelerate = float(data[idx++]) / float(RAW_BIT_PRECISION);
                if (bitmask & (1 << 5)) out.brake = float(data[idx++]) / float(RAW_BIT_PRECISION);
                if (bitmask & (1 << 6)) {
                        uint8_t buttons = data[idx++];
                        out.spinattack = (buttons & 1) != 0;
                        out.boost = (buttons & 2) != 0;
                        out.sideattack = (buttons & 4) != 0;
                }
                return out;
        }
};
