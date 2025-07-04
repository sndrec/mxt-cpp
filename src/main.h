#ifndef GAME_SIM
#define GAME_SIM

#include "godot_cpp/classes/node.hpp"
#include "godot_cpp/classes/node3D.hpp"
#include "godot_cpp/classes/multi_mesh_instance3d.hpp"
#include "godot_cpp/classes/multi_mesh.hpp"
#include "godot_cpp/classes/stream_peer_buffer.hpp"
#include "godot_cpp/variant/array.hpp"
#include "track/racetrack.h"
#include "mxt_core/heap_handler.h"
#include "mxt_core/mtxa_stack.hpp"
#include "mxt_core/player_input.h"
#include "car/car_properties.h"

namespace godot {

	class GameSim : public Node {
		GDCLASS(GameSim, Node)

	private:
		int tick;
		float tick_delta;
		HeapHandler level_data;
		HeapHandler gamestate_data;
		static const int STATE_BUFFER_LEN = 45;
		struct SavedState {
			char* data;
			int size;
		};
		SavedState state_buffer[STATE_BUFFER_LEN];
		static const int INPUT_BUFFER_LEN = STATE_BUFFER_LEN;
		PlayerInput* input_buffer = nullptr;

	protected:
		static void _bind_methods();

	public:
		bool sim_started;
		RaceTrack* current_track;
		int num_cars;
		PhysicsCar* cars;
		PhysicsCarProperties* car_properties_array = nullptr;
		MtxStack mtxa;
		godot::Node3D* car_node_container = nullptr;

		GameSim();
		~GameSim();

		void set_sim_started(const bool p_sim_started);
		bool get_sim_started();
		void set_car_node_container(godot::Node3D* p_car_node_container) { car_node_container = p_car_node_container; }
		godot::Node3D* get_car_node_container() const { return car_node_container; }
		void tick_gamesim(godot::Array player_inputs);
		void instantiate_gamesim(StreamPeerBuffer* in_buffer, godot::Array car_prop_buffers, godot::Array accel_settings);
		void destroy_gamesim();
		void render_gamesim();
		void save_state();
		void load_state(int target_tick);
		godot::PackedByteArray get_state_data(int target_tick) const;
		void set_state_data(int target_tick, godot::PackedByteArray data);
		void fix_pointers();
	};

}

#endif