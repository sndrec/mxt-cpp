#include "netcode_core.h"

#include "godot_cpp/core/class_db.hpp"
#include "godot_cpp/variant/utility_functions.hpp"
#include "godot_cpp/variant/variant.hpp"
#include "godot_cpp/classes/time.hpp"

using namespace godot;

void NetcodeCore::_bind_methods() {
    ClassDB::bind_method(D_METHOD("build_predicted_frame", "player_ids", "my_id", "existing", "prev_frame", "neutral_bytes"), &NetcodeCore::build_predicted_frame);
    ClassDB::bind_method(D_METHOD("recalc_future_predictions", "start_tick", "local_tick", "player_ids", "my_id", "input_history", "authoritative_inputs", "neutral_bytes"), &NetcodeCore::recalc_future_predictions);
    ClassDB::bind_method(D_METHOD("client_broadcast_apply", "game_sim", "last_tick", "inputs", "state", "latest_state_tick", "local_tick", "player_ids", "my_id", "input_history", "authoritative_inputs", "neutral_bytes"), &NetcodeCore::client_broadcast_apply);
}


static inline float lerp_f(float a, float b, float t) { return a + (b - a) * t; }

static PackedByteArray decay_predicted_bytes(const PackedByteArray &prev_bytes) {
    const int RAW = 254;
    float strafe_left = 0.0f, strafe_right = 0.0f, steer_h = 0.0f, steer_v = 0.0f, accel = 0.0f, brake = 0.0f;
    uint8_t buttons = 0;
    if (prev_bytes.size() > 0) {
        const uint8_t *data = prev_bytes.ptr();
        int idx = 0;
        uint8_t bitmask = data[idx++];
        auto get_u8 = [&]() -> uint8_t { return data[idx++]; };
        auto deq_axis = [&](uint8_t q)->float { return (float(q) / float(RAW)) * 2.0f - 1.0f; };
        auto deq_trig = [&](uint8_t q)->float { return float(q) / float(RAW); };
        if (bitmask & (1 << 0)) strafe_left = deq_trig(get_u8());
        if (bitmask & (1 << 1)) strafe_right = deq_trig(get_u8());
        if (bitmask & (1 << 2)) steer_h = deq_axis(get_u8());
        if (bitmask & (1 << 3)) steer_v = deq_axis(get_u8());
        if (bitmask & (1 << 4)) accel = deq_trig(get_u8());
        if (bitmask & (1 << 5)) brake = deq_trig(get_u8());
        if (bitmask & (1 << 6)) buttons = get_u8();
    }
    strafe_left  = lerp_f(strafe_left, 0.0f, 0.25f);
    strafe_right = lerp_f(strafe_right, 0.0f, 0.25f);
    steer_h      = lerp_f(steer_h, 0.0f, 0.25f);
    steer_v      = lerp_f(steer_v, 0.0f, 0.25f);

    auto quant_axis = [&](float v)->uint8_t {
        if (v < -1.0f) v = -1.0f; if (v > 1.0f) v = 1.0f;
        float t = (v + 1.0f) * 0.5f;
        int q = int(t * float(RAW) + 0.5f);
        if (q < 0) q = 0; if (q > RAW) q = RAW;
        return uint8_t(q);
    };
    auto quant_trig = [&](float v)->uint8_t {
        if (v < 0.0f) v = 0.0f; if (v > 1.0f) v = 1.0f;
        int q = int(v * float(RAW) + 0.5f);
        if (q < 0) q = 0; if (q > RAW) q = RAW;
        return uint8_t(q);
    };

    PackedByteArray out;
    out.append(uint8_t(0));
    uint8_t bitmask = 0;
    auto push = [&](uint8_t v){ out.append(v); };
    uint8_t q;
    q = quant_trig(strafe_left); if (q != 0) { bitmask |= 1 << 0; push(q); }
    q = quant_trig(strafe_right); if (q != 0) { bitmask |= 1 << 1; push(q); }
    q = quant_axis(steer_h); if (q != (RAW/2)) { bitmask |= 1 << 2; push(q); }
    q = quant_axis(steer_v); if (q != (RAW/2)) { bitmask |= 1 << 3; push(q); }
    q = quant_trig(accel); if (q != 0) { bitmask |= 1 << 4; push(q); }
    q = quant_trig(brake); if (q != 0) { bitmask |= 1 << 5; push(q); }
    if (buttons != 0) { bitmask |= 1 << 6; push(buttons); }
    out.set(0, bitmask);
    return out;
}
Array NetcodeCore::build_predicted_frame(Array player_ids, int64_t my_id, Array existing, Array prev_frame, PackedByteArray neutral_bytes) {
    Array frame_inputs;
    const int count = player_ids.size();
    frame_inputs.resize(count);
    for (int i = 0; i < count; ++i) {
        int64_t pid = (int64_t)player_ids[i];
        if (pid == my_id) {
            PackedByteArray local_bytes = neutral_bytes;
            if (existing.size() == count && Variant(existing[i]).get_type() == Variant::PACKED_BYTE_ARRAY) {
                local_bytes = (PackedByteArray)existing[i];
            }
            frame_inputs[i] = local_bytes;
        } else {
            PackedByteArray prev_bytes = neutral_bytes;
            if (prev_frame.size() == count && Variant(prev_frame[i]).get_type() == Variant::PACKED_BYTE_ARRAY) {
                prev_bytes = (PackedByteArray)prev_frame[i];
            }
            frame_inputs[i] = decay_predicted_bytes(prev_bytes);
        }
    }
    return frame_inputs;
}

void NetcodeCore::recalc_future_predictions(int64_t start_tick,
                                            int64_t local_tick,
                                            Array player_ids,
                                            int64_t my_id,
                                            Dictionary input_history,
                                            Dictionary authoritative_inputs,
                                            PackedByteArray neutral_bytes) {
    int64_t tick = start_tick;
    while (tick < local_tick) {
        Variant key_tick = Variant(tick);
        if (authoritative_inputs.has(key_tick)) {
            input_history[key_tick] = authoritative_inputs[key_tick];
            authoritative_inputs.erase(key_tick);
            tick += 1;
            continue;
        }
        Array prev_frame;
        if (input_history.has(Variant(tick - 1))) {
            prev_frame = (Array)input_history[Variant(tick - 1)];
        }
        Array existing;
        if (input_history.has(key_tick)) {
            existing = (Array)input_history[key_tick];
        }
        Array frame = build_predicted_frame(player_ids, my_id, existing, prev_frame, neutral_bytes);
        input_history[key_tick] = frame;
        tick += 1;
    }
}


#include "main.h"
using godot::Variant;

godot::Dictionary NetcodeCore::client_broadcast_apply(
    godot::Object *game_sim_obj,
    int64_t last_tick,
    godot::Array inputs,
    godot::PackedByteArray state,
    int64_t latest_state_tick,
    int64_t local_tick,
    godot::Array player_ids,
    int64_t my_id,
    godot::Dictionary input_history,
    godot::Dictionary authoritative_inputs,
    godot::PackedByteArray neutral_bytes
) {
    Dictionary out;
    GameSim *sim = Object::cast_to<GameSim>(game_sim_obj);
    if (!sim) {
        out["latest_state_tick"] = latest_state_tick;
        out["local_tick"] = local_tick;
        out["state_us"] = (int64_t)0;
        out["replay_us"] = (int64_t)0;
        return out;
    }
    int64_t state_us = 0;
    int64_t replay_us = 0;

    if (inputs.size() > 0) {
        int start = int(last_tick) - inputs.size() + 1;
        for (int i = 0; i < inputs.size(); ++i) {
            int tick = start + i;
            Array frame = inputs[i];
            authoritative_inputs[Variant(tick)] = frame;
            input_history[Variant(tick)] = frame;
        }
    }

    int64_t base_start_tick = -1;
    if (state.size() > 0) {
        int64_t t0 = Time::get_singleton()->get_ticks_usec();
        sim->set_state_data((int)last_tick, state);
        sim->load_state((int)last_tick);
        latest_state_tick = last_tick;
        local_tick = local_tick < (last_tick + 1) ? (last_tick + 1) : local_tick;
        base_start_tick = last_tick + 1;
        int64_t t1 = Time::get_singleton()->get_ticks_usec();
        state_us += (t1 - t0);
    }

    if (inputs.size() > 0 && base_start_tick == -1) {
        int start = int(last_tick) - inputs.size() + 1;
        int baseline = (int)((latest_state_tick > (start - 1)) ? latest_state_tick : (start - 1));
        sim->load_state(baseline);
        base_start_tick = (latest_state_tick + 1 > start) ? (int)(latest_state_tick + 1) : start;
        recalc_future_predictions(start + 1, local_tick, player_ids, my_id, input_history, authoritative_inputs, neutral_bytes);
    }

    if (base_start_tick != -1) {
        int64_t t0 = Time::get_singleton()->get_ticks_usec();
        for (int cur = (int)base_start_tick; cur < local_tick; ++cur) {
            Variant k = Variant(cur);
            if (input_history.has(k)) {
                Array frame = input_history[k];
                sim->tick_gamesim(frame);
            }
        }
        sim->render_gamesim();
        int64_t t1 = Time::get_singleton()->get_ticks_usec();
        replay_us += (t1 - t0);
    }

    out["latest_state_tick"] = latest_state_tick;
    out["local_tick"] = local_tick;
    out["state_us"] = state_us;
    out["replay_us"] = replay_us;
    return out;
}
