#pragma once

#include "godot_cpp/classes/input.hpp"
#include "godot_cpp/variant/dictionary.hpp"
#include "godot_cpp/variant/packed_byte_array.hpp"
#include <algorithm>
#include <cmath>

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
                return from_raw(arr.ptr(), arr.size());
        }

        static int encoded_raw_size_from_mask(uint8_t bitmask)
        {
                int size = 1;
                if (bitmask & (1 << 0)) ++size;
                if (bitmask & (1 << 1)) ++size;
                if (bitmask & (1 << 2)) ++size;
                if (bitmask & (1 << 3)) ++size;
                if (bitmask & (1 << 6)) ++size;
                return size;
        }

        static PlayerInput from_raw_consumed(const uint8_t *data, int size, int *consumed)
        {
                PlayerInput out{};
                int idx = 0;
                if (consumed)
                        *consumed = 0;
                if (!data || size <= 0)
                        return out;
                uint8_t bitmask = data[idx++];
                const int expected_size = encoded_raw_size_from_mask(bitmask);
                if (size < expected_size)
                        return out;
                if ((bitmask & (1 << 0)) && idx < size) out.strafe_left = float(data[idx++]) / float(RAW_BIT_PRECISION);
                if ((bitmask & (1 << 1)) && idx < size) out.strafe_right = float(data[idx++]) / float(RAW_BIT_PRECISION);
                if ((bitmask & (1 << 2)) && idx < size) out.steer_horizontal = (float(data[idx++]) / float(RAW_BIT_PRECISION)) * 2.0f - 1.0f;
                if ((bitmask & (1 << 3)) && idx < size) out.steer_vertical = (float(data[idx++]) / float(RAW_BIT_PRECISION)) * 2.0f - 1.0f;
                out.accelerate = (bitmask & (1 << 4)) ? 1.0f : 0.0f;
                out.brake = (bitmask & (1 << 5)) ? 1.0f : 0.0f;
                if (bitmask & (1 << 6)) {
                        if (idx >= size)
                                return out;
                        uint8_t buttons = data[idx++];
                        out.spinattack = (buttons & 1) != 0;
                        out.boost = (buttons & 2) != 0;
                        out.sideattack = (buttons & 4) != 0;
                }
                if (consumed)
                        *consumed = idx;
                return out;
        }

        static PlayerInput from_raw(const uint8_t *data, int size)
        {
                return from_raw_consumed(data, size, nullptr);
        }

        static uint8_t quantize_axis(float v)
        {
                v = std::max(-1.0f, std::min(1.0f, v));
                const float scaled = ((v + 1.0f) * 0.5f) * float(RAW_BIT_PRECISION);
                return static_cast<uint8_t>(std::floor(scaled + 0.5f));
        }

        static uint8_t quantize_trigger(float v)
        {
                v = std::max(0.0f, std::min(1.0f, v));
                const float scaled = v * float(RAW_BIT_PRECISION);
                return static_cast<uint8_t>(std::floor(scaled + 0.5f));
        }

        static godot::PackedByteArray to_bytes(const PlayerInput &input)
        {
                uint8_t data[8] = {};
                int idx = encode_to_raw(input, data, sizeof(data));
                godot::PackedByteArray out;
                out.resize(idx);
                for (int i = 0; i < idx; ++i) {
                        out.set(i, data[i]);
                }
                return out;
        }

        static int encode_to_raw(const PlayerInput &input, uint8_t *data, int capacity)
        {
                if (!data || capacity <= 0)
                        return 0;
                int idx = 1;
                uint8_t bitmask = 0;

                uint8_t q = quantize_trigger(input.strafe_left);
                if (q != TRIGGER_NEUTRAL && idx < capacity) {
                        bitmask |= 1 << 0;
                        data[idx++] = q;
                }

                q = quantize_trigger(input.strafe_right);
                if (q != TRIGGER_NEUTRAL && idx < capacity) {
                        bitmask |= 1 << 1;
                        data[idx++] = q;
                }

                q = quantize_axis(input.steer_horizontal);
                if (q != AXIS_NEUTRAL && idx < capacity) {
                        bitmask |= 1 << 2;
                        data[idx++] = q;
                }

                q = quantize_axis(input.steer_vertical);
                if (q != AXIS_NEUTRAL && idx < capacity) {
                        bitmask |= 1 << 3;
                        data[idx++] = q;
                }

                if (input.accelerate > 0.0f) {
                        bitmask |= 1 << 4;
                }

                if (input.brake > 0.0f) {
                        bitmask |= 1 << 5;
                }

                uint8_t buttons = 0;
                if (input.spinattack) buttons |= 1;
                if (input.boost) buttons |= 2;
                if (input.sideattack) buttons |= 4;
                if (buttons != 0 && idx < capacity) {
                        bitmask |= 1 << 6;
                        data[idx++] = buttons;
                }

                data[0] = bitmask;
                return idx;
        }

        static PlayerInput quantized(const PlayerInput &input)
        {
                PlayerInput out{};
                uint8_t q = quantize_trigger(input.strafe_left);
                if (q != TRIGGER_NEUTRAL)
                        out.strafe_left = float(q) / float(RAW_BIT_PRECISION);
                q = quantize_trigger(input.strafe_right);
                if (q != TRIGGER_NEUTRAL)
                        out.strafe_right = float(q) / float(RAW_BIT_PRECISION);
                q = quantize_axis(input.steer_horizontal);
                if (q != AXIS_NEUTRAL)
                        out.steer_horizontal = (float(q) / float(RAW_BIT_PRECISION)) * 2.0f - 1.0f;
                q = quantize_axis(input.steer_vertical);
                if (q != AXIS_NEUTRAL)
                        out.steer_vertical = (float(q) / float(RAW_BIT_PRECISION)) * 2.0f - 1.0f;
                out.accelerate = input.accelerate > 0.0f ? 1.0f : 0.0f;
                out.brake = input.brake > 0.0f ? 1.0f : 0.0f;
                out.spinattack = input.spinattack;
                out.boost = input.boost;
                out.sideattack = input.sideattack;
                return out;
        }
};
