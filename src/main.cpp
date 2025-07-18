#include "main.h"
#include "godot_cpp/core/class_db.hpp"
#include "godot_cpp/classes/engine.hpp"
#include "godot_cpp/variant/utility_functions.hpp"
#include "godot_cpp/core/math.hpp"
#include "mxt_core/curve.h"
#include "mxt_core/enums.h"
#include "track/racetrack.h"
#include "track/trigger_collider.h"
#include "track/road_modulation.h"
#include "track/road_embed.h"
#include "car/physics_car.h"
#include "godot_cpp/variant/array.hpp"
#include "godot_cpp/variant/packed_byte_array.hpp"
#include <chrono>
#include <cfenv>
#include <cstdlib>
#include <algorithm>
#include "mxt_core/debug.hpp"

using namespace godot;

void GameSim::_bind_methods()
{
	ClassDB::bind_method(D_METHOD("instantiate_gamesim", "lvldat_buf", "car_prop_buffers", "accel_settings"), &GameSim::instantiate_gamesim);
	ClassDB::bind_method(D_METHOD("destroy_gamesim"), &GameSim::destroy_gamesim);
	ClassDB::bind_method(D_METHOD("tick_gamesim", "player_inputs"), &GameSim::tick_gamesim);
	ClassDB::bind_method(D_METHOD("render_gamesim"), &GameSim::render_gamesim);
	ClassDB::bind_method(D_METHOD("get_sim_started"), &GameSim::get_sim_started);
	ClassDB::bind_method(D_METHOD("set_sim_started", "p_sim_started"), &GameSim::set_sim_started);
	ClassDB::bind_method(D_METHOD("save_state"), &GameSim::save_state);
	ClassDB::bind_method(D_METHOD("load_state", "target_tick"), &GameSim::load_state);
	ClassDB::bind_method(D_METHOD("get_state_data", "target_tick"), &GameSim::get_state_data);
	ClassDB::bind_method(D_METHOD("set_state_data", "target_tick", "data"), &GameSim::set_state_data);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "sim_started"), "set_sim_started", "get_sim_started");
	ClassDB::bind_method(D_METHOD("get_car_node_container"), &GameSim::get_car_node_container);
	ClassDB::bind_method(D_METHOD("set_car_node_container", "p_car_node_container"), &GameSim::set_car_node_container);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "car_node_container", PROPERTY_HINT_RESOURCE_TYPE, "Node3D"), "set_car_node_container", "get_car_node_container");
};

GameSim::GameSim()
{
	tick = 0;
	tick_delta = 1.0f / 60.0f;
	sim_started = false;
	for (int i = 0; i < STATE_BUFFER_LEN; i++)
	{
		state_buffer[i].data = nullptr;
		state_buffer[i].size = 0;
	}
	input_buffer = nullptr;
};

GameSim::~GameSim()
{
	destroy_gamesim();
	for (int i = 0; i < STATE_BUFFER_LEN; i++)
	{
		if (state_buffer[i].data)
		{
			::free(state_buffer[i].data);
			state_buffer[i].data = nullptr;
		}
	}
	if (input_buffer) {
		::free(input_buffer);
		input_buffer = nullptr;
	}
};

void GameSim::set_sim_started(const bool p_sim_started)
{
	sim_started = p_sim_started;
}

bool GameSim::get_sim_started()
{
	return sim_started;
}

void GameSim::tick_gamesim(godot::Array player_inputs)
{
	//godot::Object* dd3d = godot::Engine::get_singleton()->get_singleton("DebugDraw3D");

	std::fesetround(FE_TONEAREST);
	std::feclearexcept(FE_ALL_EXCEPT);

	auto start = std::chrono::high_resolution_clock::now();
	int buf_index = tick % INPUT_BUFFER_LEN;
	PlayerInput* slot = input_buffer + buf_index * num_cars;
	for (int i = 0; i < num_cars; i++)
	{
		PlayerInput inp = PlayerInput::from_neutral();
		if (i < player_inputs.size()) {
			Variant::Type t = player_inputs[i].get_type();
			if (t == godot::Variant::PACKED_BYTE_ARRAY) {
				inp = PlayerInput::from_bytes(player_inputs[i]);
			} else if (t == godot::Variant::DICTIONARY) {
				inp = PlayerInput::from_dict(player_inputs[i]);
			}
		}
		slot[i] = inp;
		cars[i].tick(inp, tick);
	}
	for (int i = 0; i < num_cars; i++)
	{
		for (int j = i + 1; j < num_cars; j++)
		{
			cars[i].handle_machine_v_machine_collision(cars[j]);
		}
	}
	for (int i = 0; i < num_cars; i++)
	{
		cars[i].post_tick();
	}
	//for (int i = 0; i < num_cars; i++)
	//{
	//	if (i == 0){
	//		CollisionData collision;
	//		godot::Vector3 p0 = cars[i].position + godot::Vector3(0, 5, 3);
	//		godot::Vector3 p1 = cars[i].position + godot::Vector3(0, -100, 3);
	//		current_track->cast_vs_track(collision, p0, p1, CAST_FLAGS::WANTS_TRACK, cars[i].current_collision_checkpoint);
	//		if (collision.collided){
	//			dd3d->call("draw_arrow", collision.collision_point, collision.collision_point + collision.collision_normal * 2, godot::Color(0.0f, 1.0f, 0.0f), 0.25, true, _TICK_DELTA);
	//		}
	//		dd3d->call("draw_arrow", p0, p1, godot::Color(1.0f, 0.0f, 0.0f), 0.25, true, _TICK_DELTA);
	//	}
	//}
	save_state();

	//auto elapsed = std::chrono::high_resolution_clock::now() - start;
	//long long microseconds = std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count();
	//godot::Object* dd2d = godot::Engine::get_singleton()->get_singleton("DebugDraw2D");
	//dd2d->call("set_text", "frame time us", microseconds);
	
	
	tick += 1;
	//dd2d->call("set_text", "pos 1", car_positions[0]);
	
	//dd3d->call("draw_points", car_positions, 0, 1.0f, godot::Color(1.f, 0.f, 0.f), 0.0166666);
}

void GameSim::instantiate_gamesim(StreamPeerBuffer* lvldat_buf, godot::Array car_prop_buffers, godot::Array accel_settings)
{
	if (Engine::get_singleton()->is_editor_hint()) return;

	tick = 0;

	int32_t buffer_size = lvldat_buf->get_size();

	level_data.instantiate(1024 * 1024 * 16);

	gamestate_data.instantiate(1024 * 1024);
	int state_capacity = gamestate_data.get_capacity();
	for (int i = 0; i < STATE_BUFFER_LEN; i++)
	{
		state_buffer[i].data = (char*)malloc(state_capacity);
		state_buffer[i].size = 0;
	}

	current_track = level_data.allocate_object<RaceTrack>();
	current_track->num_trigger_colliders = 0;
	current_track->trigger_colliders = nullptr;

	UtilityFunctions::print("-----");
	UtilityFunctions::print(lvldat_buf->get_position());
	uint32_t header_size = lvldat_buf->get_u32();
	UtilityFunctions::print(lvldat_buf->get_position());
	String version_string = lvldat_buf->get_string(4);
	UtilityFunctions::print(lvldat_buf->get_position());
	uint32_t checkpoint_count = lvldat_buf->get_u32();
	UtilityFunctions::print(lvldat_buf->get_position());
	uint32_t segment_count = lvldat_buf->get_u32();
	UtilityFunctions::print(lvldat_buf->get_position());
	uint32_t trigger_count = 0;
	if (version_string != "v0.1" && version_string != "v0.2") {
		trigger_count = lvldat_buf->get_u32();
		UtilityFunctions::print(lvldat_buf->get_position());
	}

	std::vector<uint32_t> neighboring_checkpoint_indices;


	//DEBUG::enable_dip(DIP_SWITCH::DIP_DRAW_SEGMENT_SURF);
	//DEBUG::enable_dip(DIP_SWITCH::DIP_DRAW_CHECKPOINTS);
	//DEBUG::enable_dip(DIP_SWITCH::DIP_DRAW_TILT_CORNER_DATA);
	//DEBUG::enable_dip(DIP_SWITCH::DIP_DRAW_SEG_BOUNDS);
	// load in collision checkpoints //

	current_track->num_checkpoints = checkpoint_count;
	current_track->checkpoint_stack = level_data.allocate_array<int>(checkpoint_count);
	current_track->visit_gen = 1;
	current_track->visit_stamp = level_data.allocate_array<uint32_t>(checkpoint_count);
	current_track->checkpoints = level_data.allocate_array<CollisionCheckpoint>(checkpoint_count);

	for (int i = 0; i < checkpoint_count; i++)
	{
		current_track->checkpoints[i].position_start[0] = lvldat_buf->get_float();
		current_track->checkpoints[i].position_start[1] = lvldat_buf->get_float();
		current_track->checkpoints[i].position_start[2] = lvldat_buf->get_float();
		current_track->checkpoints[i].position_end[0] = lvldat_buf->get_float();
		current_track->checkpoints[i].position_end[1] = lvldat_buf->get_float();
		current_track->checkpoints[i].position_end[2] = lvldat_buf->get_float();
		current_track->checkpoints[i].orientation_start[0][0] = lvldat_buf->get_float();
		current_track->checkpoints[i].orientation_start[0][1] = lvldat_buf->get_float();
		current_track->checkpoints[i].orientation_start[0][2] = lvldat_buf->get_float();
		current_track->checkpoints[i].orientation_start[1][0] = lvldat_buf->get_float();
		current_track->checkpoints[i].orientation_start[1][1] = lvldat_buf->get_float();
		current_track->checkpoints[i].orientation_start[1][2] = lvldat_buf->get_float();
		current_track->checkpoints[i].orientation_start[2][0] = lvldat_buf->get_float();
		current_track->checkpoints[i].orientation_start[2][1] = lvldat_buf->get_float();
		current_track->checkpoints[i].orientation_start[2][2] = lvldat_buf->get_float();
		current_track->checkpoints[i].orientation_end[0][0] = lvldat_buf->get_float();
		current_track->checkpoints[i].orientation_end[0][1] = lvldat_buf->get_float();
		current_track->checkpoints[i].orientation_end[0][2] = lvldat_buf->get_float();
		current_track->checkpoints[i].orientation_end[1][0] = lvldat_buf->get_float();
		current_track->checkpoints[i].orientation_end[1][1] = lvldat_buf->get_float();
		current_track->checkpoints[i].orientation_end[1][2] = lvldat_buf->get_float();
		current_track->checkpoints[i].orientation_end[2][0] = lvldat_buf->get_float();
		current_track->checkpoints[i].orientation_end[2][1] = lvldat_buf->get_float();
		current_track->checkpoints[i].orientation_end[2][2] = lvldat_buf->get_float();
		current_track->checkpoints[i].orientation_start.orthonormalize();
		current_track->checkpoints[i].orientation_end.orthonormalize();
		current_track->checkpoints[i].x_radius_start = lvldat_buf->get_float();
		current_track->checkpoints[i].y_radius_start = lvldat_buf->get_float();
		current_track->checkpoints[i].x_radius_end = lvldat_buf->get_float();
		current_track->checkpoints[i].y_radius_end = lvldat_buf->get_float();
		current_track->checkpoints[i].t_start = lvldat_buf->get_float();
		current_track->checkpoints[i].t_end = lvldat_buf->get_float();
		current_track->checkpoints[i].distance = lvldat_buf->get_float();
		if (i > 0)
		{
			current_track->checkpoints[i].distance += current_track->checkpoints[i - 1].distance;
		}
		current_track->checkpoints[i].road_segment = (int)lvldat_buf->get_u32();
		current_track->checkpoints[i].start_plane.normal[0] = lvldat_buf->get_float();
		current_track->checkpoints[i].start_plane.normal[1] = lvldat_buf->get_float();
		current_track->checkpoints[i].start_plane.normal[2] = lvldat_buf->get_float();
		current_track->checkpoints[i].start_plane.d = lvldat_buf->get_float();
		current_track->checkpoints[i].end_plane.normal[0] = lvldat_buf->get_float();
		current_track->checkpoints[i].end_plane.normal[1] = lvldat_buf->get_float();
		current_track->checkpoints[i].end_plane.normal[2] = lvldat_buf->get_float();
		current_track->checkpoints[i].end_plane.d = lvldat_buf->get_float();
		current_track->checkpoints[i].x_radius_start_inv = 1.0f / fmaxf(0.001f, current_track->checkpoints[i].x_radius_start);
		current_track->checkpoints[i].y_radius_start_inv = 1.0f / fmaxf(0.001f, current_track->checkpoints[i].y_radius_start);
		current_track->checkpoints[i].x_radius_end_inv = 1.0f / fmaxf(0.001f, current_track->checkpoints[i].x_radius_end);
		current_track->checkpoints[i].y_radius_end_inv = 1.0f / fmaxf(0.001f, current_track->checkpoints[i].y_radius_end);
		int num_n_cp = (int)lvldat_buf->get_u32();

		current_track->checkpoints[i].num_neighboring_checkpoints = num_n_cp;

		current_track->checkpoints[i].neighboring_checkpoints = level_data.allocate_array<int>(num_n_cp);
		for (int n = 0; n < num_n_cp; n++)
		{
			current_track->checkpoints[i].neighboring_checkpoints[n] = (int)lvldat_buf->get_u32();
		}
	}

	// load in track segments //
	current_track->minimum_y = 0.0f;

	current_track->num_segments = segment_count;
	current_track->segments = level_data.allocate_array<TrackSegment>(segment_count);

	for (int seg = 0; seg < segment_count; seg++)
	{
		int segment_index = (int)lvldat_buf->get_u32();
		int road_type = (int)lvldat_buf->get_u32();

		// what road shape? //

		if (road_type == 0)
		{
			current_track->segments[seg].road_shape = level_data.allocate_class<RoadShape>();
			current_track->segments[seg].road_shape->shape_type = ROAD_SHAPE_TYPE::ROAD_SHAPE_FLAT;
		}
		else if (road_type == 1)
		{
			current_track->segments[seg].road_shape = level_data.allocate_class<RoadShapeCylinder>();
			current_track->segments[seg].road_shape->shape_type = ROAD_SHAPE_TYPE::ROAD_SHAPE_CYLINDER;
		}
		else if (road_type == 2)
		{
			current_track->segments[seg].road_shape = level_data.allocate_class<RoadShapeCylinderOpen>();
			current_track->segments[seg].road_shape->openness = level_data.allocate_curve_from_buffer(lvldat_buf);
			current_track->segments[seg].road_shape->shape_type = ROAD_SHAPE_TYPE::ROAD_SHAPE_CYLINDER_OPEN;
		}
		else if (road_type == 3)
		{
			current_track->segments[seg].road_shape = level_data.allocate_class<RoadShapePipe>();
			current_track->segments[seg].road_shape->shape_type = ROAD_SHAPE_TYPE::ROAD_SHAPE_PIPE;
		}
		else if (road_type == 4)
		{
			current_track->segments[seg].road_shape = level_data.allocate_class<RoadShapePipeOpen>();
			current_track->segments[seg].road_shape->openness = level_data.allocate_curve_from_buffer(lvldat_buf);
			current_track->segments[seg].road_shape->shape_type = ROAD_SHAPE_TYPE::ROAD_SHAPE_PIPE_OPEN;
		}

		// road modulations //

		int modulation_count = (int)lvldat_buf->get_u32();
		current_track->segments[seg].road_shape->num_modulations = modulation_count;

		if (modulation_count > 0)
		{
			current_track->segments[seg].road_shape->road_modulations = level_data.allocate_array<RoadModulation>(modulation_count);
			for (int mod = 0; mod < modulation_count; mod++)
			{
				current_track->segments[seg].road_shape->road_modulations[mod].modulation_effect = level_data.allocate_curve_from_buffer(lvldat_buf);
				current_track->segments[seg].road_shape->road_modulations[mod].modulation_height = level_data.allocate_curve_from_buffer(lvldat_buf);
			}
		}

		// road embeds //

		int embed_count = (int)lvldat_buf->get_u32();
		current_track->segments[seg].road_shape->num_embeds = embed_count;
		if (embed_count > 0)
		{
			current_track->segments[seg].road_shape->road_embeds = level_data.allocate_array<RoadEmbed>(embed_count);
			for (int embed = 0; embed < embed_count; embed++)
			{
				current_track->segments[seg].road_shape->road_embeds[embed].start_offset = lvldat_buf->get_float();
				current_track->segments[seg].road_shape->road_embeds[embed].end_offset = lvldat_buf->get_float();
				int desired_embed = (int)lvldat_buf->get_u32();
				UtilityFunctions::print("----");
				UtilityFunctions::print(desired_embed);
				if (desired_embed == EMBED_TYPE_TO_TERRAIN::RECHARGE){
					current_track->segments[seg].road_shape->road_embeds[embed].embed_type = TERRAIN::RECHARGE;
				}
				if (desired_embed == EMBED_TYPE_TO_TERRAIN::DIRT){
					current_track->segments[seg].road_shape->road_embeds[embed].embed_type = TERRAIN::DIRT;
				}
				if (desired_embed == EMBED_TYPE_TO_TERRAIN::ICE){
					current_track->segments[seg].road_shape->road_embeds[embed].embed_type = TERRAIN::ICE;
				}
				if (desired_embed == EMBED_TYPE_TO_TERRAIN::LAVA){
					current_track->segments[seg].road_shape->road_embeds[embed].embed_type = TERRAIN::LAVA;
				}
				if (desired_embed == EMBED_TYPE_TO_TERRAIN::HOLE){
					current_track->segments[seg].road_shape->road_embeds[embed].embed_type = TERRAIN::HOLE;
				}
				UtilityFunctions::print(current_track->segments[seg].road_shape->road_embeds[embed].embed_type);
				
				current_track->segments[seg].road_shape->road_embeds[embed].left_border = level_data.allocate_curve_from_buffer(lvldat_buf);
				current_track->segments[seg].road_shape->road_embeds[embed].right_border = level_data.allocate_curve_from_buffer(lvldat_buf);
			}
		}

		current_track->segments[seg].road_shape->owning_segment = &current_track->segments[seg];

		int pos = lvldat_buf->get_position();
		int num_keyframes = static_cast<int>(lvldat_buf->get_u32());
		lvldat_buf->seek(pos);
      // 1) allocate the SoA object itself on your heap
		{
			uintptr_t addr = reinterpret_cast<uintptr_t>(level_data.heap_allocation);
			uintptr_t mis = addr & 31;
			if (mis) {
				level_data.allocate_bytes(32 - mis);
			}
		}
		void *raw = level_data.allocate_bytes(sizeof(RoadTransformCurve));
		RoadTransformCurve *soa = new (raw) RoadTransformCurve(num_keyframes);
		current_track->segments[seg].curve_matrix = soa;

		auto align32 = [&]() {
			uintptr_t addr = reinterpret_cast<uintptr_t>(level_data.heap_allocation);
			uintptr_t mis = addr & 31;
			if (mis) {
				level_data.allocate_bytes(32 - mis);
			}
		};

		// 2) allocate each float array, after aligning
		align32();
		soa->times       = level_data.allocate_array<float>(num_keyframes);

		align32();
		soa->values      = level_data.allocate_array<float>(num_keyframes * 16);

		align32();
		soa->tangent_in  = level_data.allocate_array<float>(num_keyframes * 16);

		align32();
		soa->tangent_out = level_data.allocate_array<float>(num_keyframes * 16);

		int seg_count = num_keyframes > 0 ? num_keyframes - 1 : 0;

		align32();
		soa->inv_dt  = level_data.allocate_array<float>(seg_count);
		align32();
		soa->coef_a  = level_data.allocate_array<float>(seg_count * 16);
		align32();
		soa->coef_b  = level_data.allocate_array<float>(seg_count * 16);
		align32();
		soa->coef_c  = level_data.allocate_array<float>(seg_count * 16);
		align32();
		soa->coef_d  = level_data.allocate_array<float>(seg_count * 16);

		// 3) fill your keyframes
		for (int n = 0; n < 15; ++n) {
			int cnt = static_cast<int>(lvldat_buf->get_u32());

			for (int i = 0; i < num_keyframes; ++i) {
				float t = lvldat_buf->get_float();
				if (n == 0) soa->times[i] = t;	// write time once

				int idx = i*16 + n;
				soa->values[idx]      = lvldat_buf->get_float();
				soa->tangent_in[idx]  = lvldat_buf->get_float();
				soa->tangent_out[idx] = lvldat_buf->get_float();
			}
		}

		for (int i = 0; i < num_keyframes; ++i) {
			int idx = i*16 + 15;
			soa->values[idx]      = 0.0f;
			soa->tangent_in[idx]  = 0.0f;
			soa->tangent_out[idx] = 0.0f;
		}

		soa->last_k = 0;
		soa->precompute();

		// 4) version‐dependent rail heights
		if (version_string != "v0.1") {
			current_track->segments[seg].left_rail_height  = lvldat_buf->get_float();
			current_track->segments[seg].right_rail_height = lvldat_buf->get_float();
		} else {
			current_track->segments[seg].left_rail_height  = 5.0f;
			current_track->segments[seg].right_rail_height = 5.0f;
		}

		// calc segment lengths //

		int sample_per_kf = 32;
		float total_distance = 0.0f;
		RoadTransform latest_sample_pos;
		current_track->segments[seg].curve_matrix->sample(latest_sample_pos, 0.0f);
		for (int i = 0; i < num_keyframes - 1; i++)
		{
			for (int n = 0; n < sample_per_kf; n++)
			{
				float use_t = (float)(n + 1) / sample_per_kf;
				use_t = remap_float(
					use_t,
					0.0f,
					1.0f,
					soa->times[i],
					soa->times[i + 1]
					);
				RoadTransform new_sample_pos;
				current_track->segments[seg].curve_matrix->sample(new_sample_pos, use_t);
				total_distance += latest_sample_pos.t3d.origin.distance_to(new_sample_pos.t3d.origin);
				latest_sample_pos = new_sample_pos;
			}
		}
		current_track->segments[seg].segment_length = total_distance;
		const int bx = 16;
		const int by = 32;
		for (int x = 0; x < bx; x++)
		{
			for (int y = 0; y < by; y++)
			{
				godot::Vector2 use_t((float(x) / (bx - 1)) * 2.0f - 1.0f, float(y) / (by - 1));
				godot::Transform3D use_pos;
				current_track->segments[seg].road_shape->get_oriented_transform_at_time(use_pos, use_t);
				if (use_pos.origin.y < current_track->minimum_y)
				{
					current_track->minimum_y = use_pos.origin.y;
				}
				//current_track->segments[seg].road_shape->get_position_at_time(use_pos, use_t);
				if (x == 0 && y == 0)
				{
					current_track->segments[seg].bounds.position = use_pos.origin;
					current_track->segments[seg].bounds.size = godot::Vector3();
				}
				current_track->segments[seg].bounds.expand_to(use_pos.origin);
				current_track->segments[seg].bounds.expand_to(use_pos.origin + use_pos.basis[1] * 25.f);
			}
		}
		current_track->segments[seg].bounds.grow_by(5.f);
		current_track->segments[seg].checkpoint_start = -1;
		current_track->segments[seg].checkpoint_run_length = 0;
		for (int i = 0; i < current_track->num_checkpoints; i++)
		{
			if (current_track->checkpoints[i].road_segment == seg)
			{
				if (current_track->segments[seg].checkpoint_start == -1)
				{
					current_track->segments[seg].checkpoint_start = i;
				}
				current_track->segments[seg].checkpoint_run_length++;
			}
		}
	}

	current_track->minimum_y -= 250.0f;

	if (trigger_count > 0) {
		current_track->num_trigger_colliders = trigger_count;
		current_track->trigger_colliders = level_data.allocate_array<TriggerCollider*>(trigger_count);
		for (uint32_t t = 0; t < trigger_count; ++t) {
			uint32_t type_val = lvldat_buf->get_u32();
			uint32_t seg_idx  = lvldat_buf->get_u32();
			uint32_t cp_idx   = lvldat_buf->get_u32();

			godot::Basis b;
			b[0][0] = lvldat_buf->get_float();
			b[0][1] = lvldat_buf->get_float();
			b[0][2] = lvldat_buf->get_float();
			b[1][0] = lvldat_buf->get_float();
			b[1][1] = lvldat_buf->get_float();
			b[1][2] = lvldat_buf->get_float();
			b[2][0] = lvldat_buf->get_float();
			b[2][1] = lvldat_buf->get_float();
			b[2][2] = lvldat_buf->get_float();
			godot::Vector3 origin;
			origin.x = lvldat_buf->get_float();
			origin.y = lvldat_buf->get_float();
			origin.z = lvldat_buf->get_float();
			godot::Transform3D inv_t(b.transposed(), origin);

			godot::Vector3 ext;
			ext.x = lvldat_buf->get_float();
			ext.y = lvldat_buf->get_float();
			ext.z = lvldat_buf->get_float();

			TriggerCollider* trig = nullptr;
			switch (type_val) {
			case TRIGGER_TYPE::DASHPLATE:
				trig = gamestate_data.allocate_class<Dashplate>();
				break;
			case TRIGGER_TYPE::JUMPPLATE:
				trig = gamestate_data.allocate_class<Jumpplate>();
				break;
			case TRIGGER_TYPE::MINE:
				trig = gamestate_data.allocate_class<Mine>();
				break;
			default:
				// TODO: assert that we never reach here!
				break;
			}
			trig->segment_index = seg_idx;
			trig->checkpoint_index = cp_idx;
			trig->inv_transform = inv_t;
			trig->transform = inv_t.affine_inverse();
			trig->half_extents = ext;
			trig->current_track = current_track;
			current_track->trigger_colliders[t] = trig;
		}
	}


	int requested_cars = car_prop_buffers.size() > 0 ? car_prop_buffers.size() : 1;
	PhysicsCarProperties* props_array = nullptr;
	cars = gamestate_data.create_and_allocate_cars(requested_cars, &props_array);
	car_properties_array = props_array;
	num_cars = requested_cars;
	for (int i = 0; i < num_cars; i++)
	{
		cars[i].mtxa = &mtxa;
		cars[i].current_track = current_track;
		if (i < car_prop_buffers.size()) {
			godot::PackedByteArray arr = car_prop_buffers[i];
               // StreamPeerBuffer inherits Reference; using Ref ensures
               // the object is freed when 'pb' goes out of scope.
			godot::Ref<godot::StreamPeerBuffer> pb = godot::Ref<godot::StreamPeerBuffer>(memnew(godot::StreamPeerBuffer));
			pb->set_data_array(arr);
			*(cars[i].car_properties) = PhysicsCarProperties::deserialize(*pb);
		}
		if (i < accel_settings.size() && accel_settings[i].get_type() == godot::Variant::FLOAT) {
			cars[i].m_accel_setting = accel_settings[i];
		}
		cars[i].initialize_machine();

                // Determine spawn transform at the end of the last track segment
		int seg_idx = current_track->num_segments - 1;
		const int columns = 6;
		const float column_width_start = -0.6f;
		const float column_width_end = 0.6f;
		const float row_spacing = 20.0f;
		const float start_offset = 40.0f;

		float distance_back = start_offset + i * 10;
		while (seg_idx > 0 && distance_back > current_track->segments[seg_idx].segment_length) {
			distance_back -= current_track->segments[seg_idx].segment_length;
			seg_idx -= 1;
		}
		if (seg_idx < 0) {
			seg_idx = 0;
			distance_back = 0.0f;
		}

		const TrackSegment &spawn_seg = current_track->segments[seg_idx];
		float t_y = remap_float(distance_back, 0.0f, spawn_seg.segment_length, 1.0f, 0.0f);
		float t_x = remap_float(static_cast<float>(i % columns), 0.0f, static_cast<float>(columns - 1), column_width_start, column_width_end);

		godot::Transform3D spawn_transform;
		spawn_seg.road_shape->get_oriented_transform_at_time(spawn_transform, godot::Vector2(t_x, t_y));
		spawn_transform.basis.transpose();
		spawn_transform.basis.orthonormalize();
		spawn_transform.basis = spawn_transform.basis.rotated(spawn_transform.basis.get_column(1), Math_PI);
		godot::Vector3 up_offset = spawn_transform.basis.get_column(1) * 0.1f;

		cars[i].position_current = spawn_transform.origin + up_offset;
		cars[i].position_old = spawn_transform.origin + up_offset;
		cars[i].position_old_2 = spawn_transform.origin + up_offset;
		cars[i].position_old_dupe = spawn_transform.origin + up_offset;
		cars[i].position_bottom = spawn_transform.xform(godot::Vector3(0.0f, -0.1f, 0.0f));

		cars[i].mtxa->push();
		cars[i].mtxa->cur->origin = spawn_transform.origin + up_offset;
		cars[i].basis_physical.basis = spawn_transform.basis;
		cars[i].basis_physical_other.basis = spawn_transform.basis;
		cars[i].rotate_mtxa_from_diff_btwn_machine_front_and_back();
		cars[i].mtxa->pop();

		cars[i].transform_visual = spawn_transform;
		cars[i].track_surface_normal = spawn_transform.basis.get_column(1);
	}

	input_buffer = static_cast<PlayerInput*>(malloc(sizeof(PlayerInput) * INPUT_BUFFER_LEN * num_cars));
	for (int i = 0; i < INPUT_BUFFER_LEN * num_cars; i++) {
		input_buffer[i] = PlayerInput::from_neutral();
	}

	sim_started = true;
	UtilityFunctions::print("finished constructing level!");
	UtilityFunctions::print("level data size:");
	UtilityFunctions::print(level_data.get_size());
	UtilityFunctions::print("gamestate size:");
	UtilityFunctions::print(gamestate_data.get_size());
	UtilityFunctions::print("trigger objects:");
	UtilityFunctions::print(trigger_count);

	if (!car_node_container) {
		UtilityFunctions::print("car_node_container is null");
		return;
	}
	if (car_node_container == nullptr) {
		UtilityFunctions::print("container is null");
		return;
	}
};

void GameSim::destroy_gamesim()
{
        if (sim_started)
        {
                if (current_track) {
                        current_track->num_trigger_colliders = 0;
                        current_track->trigger_colliders = nullptr;
                }
                level_data.free_heap();
                gamestate_data.free_heap();
		for (int i = 0; i < STATE_BUFFER_LEN; i++)
		{
			if (state_buffer[i].data)
			{
				::free(state_buffer[i].data);
				state_buffer[i].data = nullptr;
			}
		}
                if (input_buffer) {
                        ::free(input_buffer);
                        input_buffer = nullptr;
                }
                sim_started = false;
                tick = 0;
                current_track = nullptr;
        }
};

void GameSim::render_gamesim() {
	if (!sim_started || !car_node_container || !cars) {
		return;
	}

	if (car_node_container == nullptr) {
		return;
	}

	TypedArray<godot::Node> vis_cars = car_node_container->get_children();
	//mtxa.push();
	for (int i = 0; i < vis_cars.size(); i++) {
		//mtxa.assign(cars[i].basis_physical);
		//mtxa.cur->origin = cars[i].position_current;
		//cars[i].create_machine_visual_transform();
		vis_cars[i].set("position_current", cars[i].position_current);
		vis_cars[i].set("velocity", cars[i].velocity);
		vis_cars[i].set("velocity_angular", cars[i].velocity_angular);
		vis_cars[i].set("velocity_local", cars[i].velocity_local);
		vis_cars[i].set("basis_physical", cars[i].basis_physical);
		//vis_cars[i].set("transform_visual", cars[i].transform_visual);
		vis_cars[i].set("base_speed", cars[i].base_speed);
		vis_cars[i].set("boost_turbo", cars[i].boost_turbo);
		vis_cars[i].set("race_start_charge", cars[i].race_start_charge);
		vis_cars[i].set("speed_kmh", cars[i].speed_kmh);
		vis_cars[i].set("air_tilt", cars[i].air_tilt);
		vis_cars[i].set("energy", cars[i].energy);
		vis_cars[i].set("lap_progress", cars[i].lap_progress);
		vis_cars[i].set("checkpoint_fraction", cars[i].checkpoint_fraction);
		vis_cars[i].set("boost_frames", cars[i].boost_frames);
		vis_cars[i].set("boost_frames_manual", cars[i].boost_frames_manual);
		vis_cars[i].set("current_checkpoint", cars[i].current_checkpoint);
		vis_cars[i].set("lap", cars[i].lap);
		vis_cars[i].set("air_time", cars[i].air_time);
		vis_cars[i].set("machine_state", cars[i].machine_state);
		vis_cars[i].set("terrain_state", cars[i].terrain_state);
		vis_cars[i].set("frames_since_start_2", cars[i].frames_since_start_2);
		vis_cars[i].set("tilt_fl_state", cars[i].tilt_fl.state);
		vis_cars[i].set("tilt_fr_state", cars[i].tilt_fr.state);
		vis_cars[i].set("tilt_bl_state", cars[i].tilt_bl.state);
		vis_cars[i].set("tilt_br_state", cars[i].tilt_br.state);
		vis_cars[i].set("input_strafe", cars[i].input_strafe);
		vis_cars[i].set("turn_reaction_input", cars[i].turn_reaction_input);
		vis_cars[i].set("g_anim_timer", cars[i].g_anim_timer);
		vis_cars[i].set("state_2", cars[i].state_2);
		vis_cars[i].set("tilt_fl_offset", cars[i].tilt_fl.offset);
		vis_cars[i].set("tilt_bl_offset", cars[i].tilt_bl.offset);
		vis_cars[i].set("stat_weight", cars[i].stat_weight);
		vis_cars[i].set("stat_strafe", cars[i].stat_strafe);
		vis_cars[i].set("input_strafe_1_6", cars[i].input_strafe_1_6);
		vis_cars[i].set("weight_derived_1", cars[i].weight_derived_1);
		vis_cars[i].set("weight_derived_2", cars[i].weight_derived_2);
		vis_cars[i].set("weight_derived_3", cars[i].weight_derived_3);
		vis_cars[i].set("visual_rotation", cars[i].visual_rotation);
		vis_cars[i].set("spinattack_angle", cars[i].spinattack_angle);
		vis_cars[i].set("spinattack_direction", cars[i].spinattack_direction);
		vis_cars[i].set("visual_shake_mult", cars[i].visual_shake_mult);
	}
	//mtxa.pop();
	if (DEBUG::dip_enabled(DIP_SWITCH::DIP_DRAW_CHECKPOINTS))
	{
		for (int i = 0; i < current_track->num_checkpoints; i++)
		{
			current_track->checkpoints[i].debug_draw();
		}
	}
	if (DEBUG::dip_enabled(DIP_SWITCH::DIP_DRAW_SEG_BOUNDS))
	{
		godot::Object* dd3d = godot::Engine::get_singleton()->get_singleton("DebugDraw3D");
		for (int i = 0; i < current_track->num_segments; i++)
		{
			dd3d->call("draw_aabb", current_track->segments[i].bounds, godot::Color(1.0f, 0.0f, 1.0f, 0.1f), _TICK_DELTA);
		}
	}
	if (DEBUG::dip_enabled(DIP_SWITCH::DIP_DRAW_SEGMENT_SURF))
	{
		//DEBUG::disp_text("current checkpoint", cars[0].current_checkpoint);
		int use_seg_ind = current_track->checkpoints[cars[0].current_checkpoint].road_segment;
		for (int i = 0; i < current_track->num_segments; i++)
		{
			if (i > use_seg_ind + 1 || i < use_seg_ind - 1){
				continue;
			}
			godot::Object* dd3d = godot::Engine::get_singleton()->get_singleton("DebugDraw3D");

			const int x_subdiv = 16; // Adjust as needed
			const int y_subdiv = 32;  // Adjust as needed

			for (int yi = 0; yi <= y_subdiv; yi++)
			{
				float y_frac = static_cast<float>(yi) / y_subdiv;
				float y_val = y_frac; // Y: 0.0 to 1.0

				for (int xi = 0; xi <= x_subdiv; xi++)
				{
					float x_frac = static_cast<float>(xi) / x_subdiv;
					float x_val = -1.0f + 2.0f * x_frac; // X: -1.0 to +1.0

					// Interpolated color: red to blue across X, green from 0 to 1 across Y
					float r = 1.0f - x_frac;
					float g = y_frac;
					float b = x_frac;

					godot::Vector2 shape_pos(x_val, y_val);
					godot::Transform3D road_transform;
					current_track->segments[i].road_shape->get_oriented_transform_at_time(road_transform, shape_pos);

					godot::Vector3 start = road_transform.origin;
					godot::Vector3 end = start + road_transform.basis.transposed().get_column(1) * 2.0; // arrow in local Y/up

					dd3d->call("draw_arrow", start, end, godot::Color(r, g, b), 0.5, true, _TICK_DELTA);
				}
			}
		}
	}
}

void GameSim::save_state()
{
	int index = tick % STATE_BUFFER_LEN;
	int size = gamestate_data.get_size();
	state_buffer[index].size = size;
	if (state_buffer[index].data)
	{
		memcpy(state_buffer[index].data, gamestate_data.heap_start, size);
	}
}

void GameSim::load_state(int target_tick)
{
	int index = target_tick % STATE_BUFFER_LEN;
	if (!state_buffer[index].data)
		return;
	int size = state_buffer[index].size;
	memcpy(gamestate_data.heap_start, state_buffer[index].data, size);
	gamestate_data.set_size(size);
	tick = target_tick + 1;
	fix_pointers();
}

godot::PackedByteArray GameSim::get_state_data(int target_tick) const {
	godot::PackedByteArray arr;
	int index = target_tick % STATE_BUFFER_LEN;
	if (!state_buffer[index].data)
		return arr;
	int size = state_buffer[index].size;
	arr.resize(size);
	if (size > 0) {
		memcpy(arr.ptrw(), state_buffer[index].data, size);
	}
	return arr;
}

void GameSim::set_state_data(int target_tick, godot::PackedByteArray data) {
	int index = target_tick % STATE_BUFFER_LEN;
	if (!state_buffer[index].data)
		return;
	// game state never changes in size after instantiation
	// and should always be the same size between the server and all clients
	int size = static_cast<int>(data.size());
	if (size > 0) {
	    memcpy(state_buffer[index].data, data.ptr(), size);
	    state_buffer[index].size = size;
	}
}

void GameSim::fix_pointers() {
	if (!sim_started || !cars) {
		return;
	}
	for (int i = 0; i < num_cars; ++i) {
		cars[i].mtxa = &mtxa;
		cars[i].current_track = current_track;
		if (car_properties_array) {
			cars[i].car_properties = &car_properties_array[i];
		}
	}
}