#include "main.h"
#include "godot_cpp/core/class_db.hpp"
#include "godot_cpp/classes/engine.hpp"
#include "godot_cpp/variant/utility_functions.hpp"
#include "mxt_core/curve.h"
#include "mxt_core/enums.h"
#include "track/racetrack.h"
#include "track/road_modulation.h"
#include "track/road_embed.h"
#include "car/physics_car.h"
#include <cfenv>
#include <cstdlib>
#include "mxt_core/debug.hpp"

using namespace godot;

void GameSim::_bind_methods()
{
	ClassDB::bind_method(D_METHOD("instantiate_gamesim"), &GameSim::instantiate_gamesim);
	ClassDB::bind_method(D_METHOD("destroy_gamesim"), &GameSim::destroy_gamesim);
	ClassDB::bind_method(D_METHOD("tick_gamesim"), &GameSim::tick_gamesim);
	ClassDB::bind_method(D_METHOD("render_gamesim"), &GameSim::render_gamesim);
	ClassDB::bind_method(D_METHOD("get_sim_started"), &GameSim::get_sim_started);
	ClassDB::bind_method(D_METHOD("set_sim_started", "p_sim_started"), &GameSim::set_sim_started);
	ClassDB::bind_method(D_METHOD("save_state"), &GameSim::save_state);
	ClassDB::bind_method(D_METHOD("load_state", "target_tick"), &GameSim::load_state);
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
};

void GameSim::set_sim_started(const bool p_sim_started)
{
	sim_started = p_sim_started;
}

bool GameSim::get_sim_started()
{
	return sim_started;
}

void GameSim::tick_gamesim()
{
	godot::Object* dd3d = godot::Engine::get_singleton()->get_singleton("DebugDraw3D");

	std::fesetround(FE_TONEAREST);
	std::feclearexcept(FE_ALL_EXCEPT);

	auto start = std::chrono::high_resolution_clock::now();
	for (int i = 0; i < num_cars; i++)
	{
		cars[i].tick(tick);
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

	auto elapsed = std::chrono::high_resolution_clock::now() - start;
	long long microseconds = std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count();
	godot::Object* dd2d = godot::Engine::get_singleton()->get_singleton("DebugDraw2D");
	dd2d->call("set_text", "frame time us", microseconds);
	
	
	tick += 1;
	//dd2d->call("set_text", "pos 1", car_positions[0]);
	
	//dd3d->call("draw_points", car_positions, 0, 1.0f, godot::Color(1.f, 0.f, 0.f), 0.0166666);
}

void GameSim::instantiate_gamesim(StreamPeerBuffer* lvldat_buf)
{
	if (Engine::get_singleton()->is_editor_hint()) return;


	int32_t buffer_size = lvldat_buf->get_size();

	level_data.instantiate(1024 * 1024 * 16);

	current_track = level_data.allocate_object<RaceTrack>();
	current_track->num_segments = 0;
	current_track->num_checkpoints = 0;
	current_track->segments = nullptr;
	current_track->checkpoints = nullptr;
	current_track->bounds = godot::AABB();
	current_track->checkpoint_grid.cells = nullptr;
	current_track->checkpoint_grid.dim_x = 0;
	current_track->checkpoint_grid.dim_y = 0;
	current_track->checkpoint_grid.dim_z = 0;
	current_track->checkpoint_grid.voxel_size = 0.0f;

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

	std::vector<uint32_t> neighboring_checkpoint_indices;


	//DEBUG::enable_dip(DIP_SWITCH::DIP_DRAW_SEGMENT_SURF);
	//DEBUG::enable_dip(DIP_SWITCH::DIP_DRAW_CHECKPOINTS);
	//DEBUG::enable_dip(DIP_SWITCH::DIP_DRAW_TILT_CORNER_DATA);
	// load in collision checkpoints //

	current_track->num_checkpoints = checkpoint_count;
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
		current_track->checkpoints[i].x_radius_start = lvldat_buf->get_float();
		current_track->checkpoints[i].y_radius_start = lvldat_buf->get_float();
		current_track->checkpoints[i].x_radius_end = lvldat_buf->get_float();
		current_track->checkpoints[i].y_radius_end = lvldat_buf->get_float();
		current_track->checkpoints[i].t_start = lvldat_buf->get_float();
		current_track->checkpoints[i].t_end = lvldat_buf->get_float();
		current_track->checkpoints[i].distance = lvldat_buf->get_float();
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
				current_track->segments[seg].road_shape->road_embeds[embed].embed_type = (int)lvldat_buf->get_u32();
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
			uintptr_t mis = addr & 15;
			if (mis) {
				level_data.allocate_bytes(16 - mis);
			}
		}
		void *raw = level_data.allocate_bytes(sizeof(RoadTransformCurve));
		RoadTransformCurve *soa = new (raw) RoadTransformCurve(num_keyframes);
		current_track->segments[seg].curve_matrix = soa;


		// helper to bump heap_allocation up to the next 16-byte boundary
		auto align16 = [&]() {
			uintptr_t addr = reinterpret_cast<uintptr_t>(level_data.heap_allocation);
			uintptr_t mis = addr & 15;
			if (mis) {
				level_data.allocate_bytes(16 - mis);
			}
		};

		// 2) allocate each float array, after aligning
		align16();
		soa->times       = level_data.allocate_array<float>(num_keyframes);

		align16();
		soa->values      = level_data.allocate_array<float>(num_keyframes * 16);

		align16();
		soa->tangent_in  = level_data.allocate_array<float>(num_keyframes * 16);

		align16();
		soa->tangent_out = level_data.allocate_array<float>(num_keyframes * 16);

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
		godot::Transform3D latest_sample_pos;
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
				godot::Transform3D new_sample_pos;
				current_track->segments[seg].curve_matrix->sample(new_sample_pos, use_t);
				total_distance += latest_sample_pos.origin.distance_to(new_sample_pos.origin);
				latest_sample_pos = new_sample_pos;
			}
		}
		current_track->segments[seg].segment_length = total_distance;
	}

	for (int seg = 0; seg < segment_count; seg++)
	{
		for (int x = 0; x < 16; x++)
		{
			for (int y = 0; y < 32; y++)
			{
				godot::Vector2 use_t = godot::Vector2(float(x) / 15.0f, float(y) / 31.0f);
				godot::Vector3 use_pos;
				current_track->segments[seg].road_shape->get_position_at_time(use_pos, use_t);
				if (seg == 0 && x == 0 && y == 0)
				{
					current_track->bounds.position = use_pos;
					current_track->bounds.size = godot::Vector3();
				}
				current_track->bounds.expand_to(use_pos);
			}
		}
	}

	current_track->build_checkpoint_grid(level_data, 100.0f);

	gamestate_data.instantiate(1024 * 1024);
	int state_capacity = gamestate_data.get_capacity();
	for (int i = 0; i < STATE_BUFFER_LEN; i++)
	{
		state_buffer[i].data = (char*)malloc(state_capacity);
		state_buffer[i].size = 0;
	}


	cars = gamestate_data.create_and_allocate_cars(1);
	num_cars = 1;
	for (int i = 0; i < num_cars; i++)
	{
		cars[i].mtxa = &mtxa;
		cars[i].current_track = current_track;
		cars[i].initialize_machine();
		cars[i].position_current = godot::Vector3(0.5f * (i % 16), 200.0f, 0.25f * (i / 16));
	}

	sim_started = true;
	UtilityFunctions::print("finished constructing level!");
	UtilityFunctions::print("level data size:");
	UtilityFunctions::print(level_data.get_size());
	UtilityFunctions::print("gamestate size:");
	UtilityFunctions::print(gamestate_data.get_size());

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
		sim_started = false;
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
	for (int i = 0; i < vis_cars.size(); i++) {
		vis_cars[i].set("position_current", cars[i].position_current);
		vis_cars[i].set("velocity", cars[i].velocity);
		vis_cars[i].set("velocity_angular", cars[i].velocity_angular);
		vis_cars[i].set("velocity_local", cars[i].velocity_local);
		vis_cars[i].set("basis_physical", cars[i].basis_physical);
		vis_cars[i].set("transform_visual", cars[i].transform_visual);
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
		vis_cars[i].set("frames_since_start_2", cars[i].frames_since_start_2);
		vis_cars[i].set("tilt_fl_state", cars[i].tilt_fl.state);
		vis_cars[i].set("tilt_fr_state", cars[i].tilt_fr.state);
		vis_cars[i].set("tilt_bl_state", cars[i].tilt_bl.state);
		vis_cars[i].set("tilt_br_state", cars[i].tilt_br.state);
	}
	if (DEBUG::dip_enabled(DIP_SWITCH::DIP_DRAW_CHECKPOINTS))
	{
		for (int i = 0; i < current_track->num_checkpoints; i++)
		{
			current_track->checkpoints[i].debug_draw();
		}
	}
	if (DEBUG::dip_enabled(DIP_SWITCH::DIP_DRAW_SEGMENT_SURF))
	{
		DEBUG::disp_text("current checkpoint", cars[0].current_checkpoint);
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
	tick = target_tick;
}