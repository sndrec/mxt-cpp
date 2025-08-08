#pragma once

#include "godot_cpp/classes/object.hpp"
#include "godot_cpp/variant/array.hpp"
#include "godot_cpp/variant/dictionary.hpp"
#include "godot_cpp/variant/packed_byte_array.hpp"

namespace godot {

class NetcodeCore : public Object {
    GDCLASS(NetcodeCore, Object)

protected:
    static void _bind_methods();

public:
    NetcodeCore() = default;
    ~NetcodeCore() = default;

    // Build one predicted frame for remote players given previous frame and current existing frame.
    Array build_predicted_frame(Array player_ids,
                                int64_t my_id,
                                Array existing,
                                Array prev_frame,
                                PackedByteArray neutral_bytes );

    

    




    // Apply a server broadcast on the client: merge inputs, apply state (if present),
    // recalc predictions and replay GameSim from the correct baseline. Returns a dictionary with
    // updated latest_state_tick, local_tick, and timing info in microseconds (state_us, replay_us).
    godot::Dictionary client_broadcast_apply(
        godot::Object *game_sim,
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
    );


    // Recalculate predictions for ticks [start_tick, local_tick) in-place, mirroring GDScript logic.
    void recalc_future_predictions(int64_t start_tick,
                                   int64_t local_tick,
                                   Array player_ids,
                                   int64_t my_id,
                                   Dictionary input_history,
                                   Dictionary authoritative_inputs,
                                   PackedByteArray neutral_bytes );


};

}
