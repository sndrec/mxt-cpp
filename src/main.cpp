#include "main.h"
#include "godot_cpp/core/class_db.hpp"
#include "godot_cpp/classes/engine.hpp"
#include "godot_cpp/variant/utility_functions.hpp"
#include "mxt_core/curve.h"
#include "track/curve_matrix.h"
#include "track/road_shape_base.h"
#include "track/road_modulation.h"
#include "track/road_embed.h"
#include "car/physics_car.h"
#include <cfenv>

using namespace godot;

void GameSim::_bind_methods()
{
	ClassDB::bind_method(D_METHOD("instantiate_gamesim"), &GameSim::instantiate_gamesim);
	ClassDB::bind_method(D_METHOD("destroy_gamesim"), &GameSim::destroy_gamesim);
	ClassDB::bind_method(D_METHOD("tick_gamesim"), &GameSim::tick_gamesim);
	ClassDB::bind_method(D_METHOD("render_gamesim"), &GameSim::render_gamesim);
	ClassDB::bind_method(D_METHOD("get_sim_started"), &GameSim::get_sim_started);
	ClassDB::bind_method(D_METHOD("set_sim_started", "p_sim_started"), &GameSim::set_sim_started);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "sim_started"), "set_sim_started", "get_sim_started");
	ClassDB::bind_method(D_METHOD("get_car_node_container"), &GameSim::get_car_node_container);
	ClassDB::bind_method(D_METHOD("set_car_node_container", "p_car_node_container"), &GameSim::set_car_node_container);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "car_node_container", PROPERTY_HINT_RESOURCE_TYPE, "MultiMeshInstance3D"), "set_car_node_container", "get_car_node_container");
};

GameSim::GameSim()
{
	tick = 0;
	tick_delta = 1.0f / 60.0f;
	sim_started = false;
};

GameSim::~GameSim()
{
	destroy_gamesim();
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
	//godot::Object* dd3d = godot::Engine::get_singleton()->get_singleton("DebugDraw3D");

	std::fesetround(FE_TONEAREST);
	std::feclearexcept(FE_ALL_EXCEPT);

	auto start = std::chrono::high_resolution_clock::now();
	for (int i = 0; i < num_cars; i++)
	{
		cars[i].preprocess_car();
	}
	for (int i = 0; i < num_cars; i++)
	{
		cars[i].process_car_steering();
	}
	for (int i = 0; i < num_cars; i++)
	{
		cars[i].process_car_acceleration();
	}
	for (int i = 0; i < num_cars; i++)
	{
		cars[i].process_car_road_collision();
	}
	for (int i = 0; i < num_cars; i++)
	{
		cars[i].process_car_car_collision();
	}
	for (int i = 0; i < num_cars; i++)
	{
		cars[i].postprocess_car();
	}

	auto elapsed = std::chrono::high_resolution_clock::now() - start;
	long long microseconds = std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count();
	godot::Object* dd2d = godot::Engine::get_singleton()->get_singleton("DebugDraw2D");
	dd2d->call("set_text", "frame time us", microseconds);

	//dd2d->call("set_text", "pos 1", car_positions[0]);
	
	//dd3d->call("draw_points", car_positions, 0, 1.0f, godot::Color(1.f, 0.f, 0.f), 0.0166666);
}

void GameSim::instantiate_gamesim(StreamPeerBuffer* lvldat_buf)
{
	if (Engine::get_singleton()->is_editor_hint()) return;


	int32_t buffer_size = lvldat_buf->get_size();

	level_data.instantiate(1024 * 1024 * 16);

	current_track = level_data.allocate_object<RaceTrack>();

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
		current_track->checkpoints[i].x_radius_start_inv = 1.0f / fmaxf(0.0001f, current_track->checkpoints[i].x_radius_start);
		current_track->checkpoints[i].y_radius_start_inv = 1.0f / fmaxf(0.0001f, current_track->checkpoints[i].y_radius_start);
		current_track->checkpoints[i].x_radius_end_inv = 1.0f / fmaxf(0.0001f, current_track->checkpoints[i].x_radius_end);
		current_track->checkpoints[i].y_radius_end_inv = 1.0f / fmaxf(0.0001f, current_track->checkpoints[i].y_radius_end);
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
		}
		else if (road_type == 1)
		{
			current_track->segments[seg].road_shape = level_data.allocate_class<RoadShapeCylinder>();
		}
		else if (road_type == 2)
		{
			current_track->segments[seg].road_shape = level_data.allocate_class<RoadShapeCylinderOpen>();
			current_track->segments[seg].road_shape->openness = level_data.allocate_curve_from_buffer(lvldat_buf);
		}
		else if (road_type == 3)
		{
			current_track->segments[seg].road_shape = level_data.allocate_class<RoadShapePipe>();
		}
		else if (road_type == 4)
		{
			current_track->segments[seg].road_shape = level_data.allocate_class<RoadShapePipeOpen>();
			current_track->segments[seg].road_shape->openness = level_data.allocate_curve_from_buffer(lvldat_buf);
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

		current_track->segments[seg].curve_matrix = level_data.allocate_object<RoadTransformCurve>();

		// road curve data //

		int pos = lvldat_buf->get_position();
		int num_keyframes = (int)lvldat_buf->get_u32();
		lvldat_buf->seek(pos);
		current_track->segments[seg].curve_matrix->num_keyframes = num_keyframes;
		current_track->segments[seg].curve_matrix->keyframes = level_data.allocate_array<RoadTransformCurveKeyframe>(num_keyframes);

		for (int n = 0; n < 15; n++)
		{
			num_keyframes = (int)lvldat_buf->get_u32();
			for (int i = 0; i < num_keyframes; i++)
			{
				current_track->segments[seg].curve_matrix->keyframes[i].time = lvldat_buf->get_float();
				current_track->segments[seg].curve_matrix->keyframes[i].value[n] = lvldat_buf->get_float();
				current_track->segments[seg].curve_matrix->keyframes[i].tangent_in[n] = lvldat_buf->get_float();
				current_track->segments[seg].curve_matrix->keyframes[i].tangent_out[n] = lvldat_buf->get_float();
			}
		}

		// one extra curve for alignment //

		for (int i = 0; i < num_keyframes; i++)
		{
			current_track->segments[seg].curve_matrix->keyframes[i].value[15] = 0.0f;
			current_track->segments[seg].curve_matrix->keyframes[i].tangent_in[15] = 0.0f;
			current_track->segments[seg].curve_matrix->keyframes[i].tangent_out[15] = 0.0f;
		}

		// calc segment lengths //

		int sample_per_kf = 32;
		float total_distance = 0.0f;
		godot::Vector3 latest_sample_pos = current_track->segments[seg].curve_matrix->sample(0.0f).origin;
		for (int i = 0; i < num_keyframes - 1; i++)
		{
			for (int n = 0; n < sample_per_kf; n++)
			{
				float use_t = (float)(n + 1) / sample_per_kf;
				use_t = remap_float(use_t, 0.0f, 1.0f, current_track->segments[seg].curve_matrix->keyframes[i].time, current_track->segments[seg].curve_matrix->keyframes[i + 1].time);
				godot::Vector3 new_sample_pos = current_track->segments[seg].curve_matrix->sample(use_t).origin;
				total_distance += latest_sample_pos.distance_to(new_sample_pos);
				latest_sample_pos = new_sample_pos;
			}
		}
		current_track->segments[seg].segment_length = total_distance;
	}

	gamestate_data.instantiate(1024 * 1024 * 8);

	cars = gamestate_data.create_and_allocate_cars(100);
	num_cars = 100;
	for (int i = 0; i < num_cars; i++)
	{
		cars[i].current_track = current_track;
		cars[i].position = godot::Vector3(0.5f * (i % 16), 200.0f, 0.25f * (i / 16));
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
	Ref<MultiMesh> multimesh = car_node_container->get_multimesh();
	if (multimesh.is_null()) {
		UtilityFunctions::print("MultiMesh is null");
		return;
	}
	multimesh->set_instance_count(num_cars);
};

void GameSim::destroy_gamesim()
{
	if (sim_started)
	{
		level_data.free_heap();
		gamestate_data.free_heap();
		sim_started = false;
	}
};

void GameSim::render_gamesim() {
	if (!sim_started || !car_node_container || !cars) {
		return;
	}

	Ref<MultiMesh> multimesh = car_node_container->get_multimesh();
	if (multimesh.is_null()) {
		return;
	}

	int max_render_cars = MIN(num_cars, multimesh->get_instance_count());

	for (int i = 0; i < max_render_cars; i++) {
		Transform3D car_transform = cars[i].car_transform;
		car_transform.basis.transpose();
		multimesh->set_instance_transform(i, car_transform);
	}
}