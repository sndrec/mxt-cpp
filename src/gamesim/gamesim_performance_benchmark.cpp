#include "gamesim/gamesim.h"

#include "gamesim/gamesim_internal.h"

#include "content/track_payload_validator.h"

#include <godot_cpp/classes/stream_peer_buffer.hpp>
#include <godot_cpp/core/memory.hpp>

#include <algorithm>
#include <cmath>

namespace godot {

namespace {

static constexpr int BENCHMARK_CAR_COUNT = 2;
static constexpr int SETTLE_FRAMES = 180;
static constexpr int TURN_WARMUP_FRAMES = 240;
static constexpr int TURN_MEASURE_FRAMES = 120;
static constexpr float TRACK_LENGTH = 20000.0f;
static constexpr float TRACK_HALF_WIDTH = 2000.0f;
static constexpr float RADIANS_TO_DEGREES = 57.2957795131f;

static void put_vec3(StreamPeerBuffer *buffer, float x, float y, float z) {
	buffer->put_float(x);
	buffer->put_float(y);
	buffer->put_float(z);
}

static void put_identity_basis(StreamPeerBuffer *buffer) {
	put_vec3(buffer, 1.0f, 0.0f, 0.0f);
	put_vec3(buffer, 0.0f, 1.0f, 0.0f);
	put_vec3(buffer, 0.0f, 0.0f, 1.0f);
}

static void put_transform_curve(
		StreamPeerBuffer *buffer,
		float start_value,
		float end_value,
		float start_tangent,
		float end_tangent) {
	buffer->put_u32(2);
	buffer->put_float(0.0f);
	buffer->put_float(start_value);
	buffer->put_float(start_tangent);
	buffer->put_float(start_tangent);
	buffer->put_float(1.0f);
	buffer->put_float(end_value);
	buffer->put_float(end_tangent);
	buffer->put_float(end_tangent);
}

static Ref<StreamPeerBuffer> make_flat_benchmark_track() {
	Ref<StreamPeerBuffer> buffer;
	buffer.instantiate();
	buffer->set_big_endian(false);

	buffer->put_u32(24);
	PackedByteArray version;
	version.resize(4);
	version.set(0, 'v');
	version.set(1, '0');
	version.set(2, '.');
	version.set(3, '9');
	buffer->put_data(version);
	buffer->put_u32(1); // checkpoints
	buffer->put_u32(1); // segments
	buffer->put_u32(0); // triggers
	buffer->put_u32(0); // mesh collision triangles

	// One checkpoint covers a wide, straight, flat analytic road.
	put_vec3(buffer.ptr(), 0.0f, 0.0f, 0.0f);
	put_vec3(buffer.ptr(), 0.0f, 0.0f, TRACK_LENGTH);
	put_identity_basis(buffer.ptr());
	put_identity_basis(buffer.ptr());
	buffer->put_float(TRACK_HALF_WIDTH);
	buffer->put_float(100.0f);
	buffer->put_float(TRACK_HALF_WIDTH);
	buffer->put_float(100.0f);
	buffer->put_float(0.0f);
	buffer->put_float(1.0f);
	buffer->put_float(TRACK_LENGTH);
	buffer->put_u32(0);
	put_vec3(buffer.ptr(), 0.0f, 0.0f, 1.0f);
	buffer->put_float(0.0f);
	put_vec3(buffer.ptr(), 0.0f, 0.0f, 1.0f);
	buffer->put_float(TRACK_LENGTH);
	buffer->put_u32(0); // neighbors

	buffer->put_u32(0); // segment index
	buffer->put_u32(0); // flat road
	buffer->put_u32(1); // analytic collision
	buffer->put_u32(0); // modulations
	buffer->put_u32(0); // embeds

	put_transform_curve(buffer.ptr(), 0.0f, 0.0f, 0.0f, 0.0f); // location X
	put_transform_curve(buffer.ptr(), 0.0f, 0.0f, 0.0f, 0.0f); // location Y
	put_transform_curve(buffer.ptr(), 0.0f, TRACK_LENGTH, TRACK_LENGTH, TRACK_LENGTH); // location Z
	static constexpr float IDENTITY_BASIS[9] = {
		1.0f, 0.0f, 0.0f,
		0.0f, 1.0f, 0.0f,
		0.0f, 0.0f, 1.0f};
	for (float component : IDENTITY_BASIS) {
		put_transform_curve(buffer.ptr(), component, component, 0.0f, 0.0f);
	}
	put_transform_curve(buffer.ptr(), TRACK_HALF_WIDTH, TRACK_HALF_WIDTH, 0.0f, 0.0f);
	put_transform_curve(buffer.ptr(), 1.0f, 1.0f, 0.0f, 0.0f);
	put_transform_curve(buffer.ptr(), 1.0f, 1.0f, 0.0f, 0.0f);

	buffer->put_float(0.0f); // left rail height
	buffer->put_float(0.0f); // right rail height
	buffer->put_float(0.0f);
	buffer->put_float(1.0f);
	buffer->put_float(0.0f);
	buffer->put_float(1.0f);
	buffer->seek(0);
	return buffer;
}

static SimVec3 flat_forward(const PhysicsCar &car) {
	const SimBasis basis = MXT_LOAD_TRANSFORM(*car.soa, basis_physical, car.soa_index).basis;
	SimVec3 forward = basis.get_column(2) * -1.0f;
	forward.y = 0.0f;
	const float length_squared = forward.length_squared();
	return length_squared > 0.000001f
		? forward * (1.0f / std::sqrt(length_squared))
		: SimVec3(0.0f, 0.0f, -1.0f);
}

static float signed_flat_angle(const SimVec3 &from, const SimVec3 &to) {
	const float cross_y = from.z * to.x - from.x * to.z;
	const float dot = from.x * to.x + from.z * to.z;
	return std::atan2(cross_y, dot);
}

static bool car_is_drifting(const PhysicsCar &car) {
	const PhysicsCarSoA &soa = *car.soa;
	const int point_base = car.soa_index * 4;
	for (int corner = 0; corner < 4; ++corner) {
		if ((soa.tilt_state[point_base + corner] & TILTSTATE::DRIFT) != 0u) {
			return true;
		}
	}
	return false;
}

static void place_car_on_benchmark_road(PhysicsCar &car, float lane_x) {
	PhysicsCarSoA &soa = *car.soa;
	const int lane = car.soa_index;
	const int point_base = lane * 4;
	const SimTransform transform(SimBasis(), SimVec3(lane_x, 0.5f, TRACK_LENGTH * 0.5f));
	STORE_INDEXED_VEC3(soa, position_current, lane, transform.origin);
	STORE_INDEXED_VEC3(soa, position_old, lane, transform.origin);
	STORE_INDEXED_VEC3(soa, position_old_dupe, lane, transform.origin);
	STORE_INDEXED_VEC3(soa, initial_pos, lane, transform.origin);
	STORE_INDEXED_VEC3(soa, position_bottom, lane,
		transform.xform(SimVec3(0.0f, -0.1f, 0.0f)));
	MXT_STORE_TRANSFORM(soa, basis_physical, lane, transform);
	MXT_STORE_TRANSFORM(soa, basis_physical_other, lane, transform);
	MXT_STORE_TRANSFORM(soa, transform_visual, lane, transform);
	STORE_INDEXED_VEC3(soa, track_surface_normal, lane, SimVec3(0.0f, 1.0f, 0.0f));
	STORE_INDEXED_VEC3(soa, track_surface_pos, lane,
		SimVec3(lane_x, 0.0f, TRACK_LENGTH * 0.5f));
	soa.height_above_track[lane] = 19.5f;
	soa.current_checkpoint[lane] = 0;
	soa.current_collision_checkpoint[lane] = 0;
	soa.last_ground_checkpoint[lane] = 0;
	soa.checkpoint_fraction[lane] = 0.5f;
	soa.lap_progress[lane] = 0.5f;
	soa.checkpoint_track_distance[lane] = TRACK_LENGTH * 0.5f;
	soa.last_ground_distance[lane] = TRACK_LENGTH * 0.5f;
	soa.previous_lap_distance[lane] = TRACK_LENGTH * 0.5f;
	for (int corner = 0; corner < 4; ++corner) {
		const int point = point_base + corner;
		const SimVec3 local(
			soa.tilt_offset_x[point], soa.tilt_offset_y[point], soa.tilt_offset_z[point]);
		const SimVec3 world = transform.xform(local);
		STORE_INDEXED_VEC3(soa, tilt_pos, point, world);
		STORE_INDEXED_VEC3(soa, tilt_pos_old, point, world);
		STORE_INDEXED_VEC3(soa, tilt_up_vector, point, SimVec3(0.0f, 1.0f, 0.0f));
		STORE_INDEXED_VEC3(soa, tilt_up_vector_2, point, SimVec3(0.0f, 1.0f, 0.0f));
		soa.tilt_state[point] = 0;
		soa.tilt_force[point] = 0.0f;
	}
	car.update_pitch_transform_from_machine_front_back();
}

} // namespace

bool GameSim::measure_flat_ground_steering(
		const PackedByteArray& car_properties,
		float machine_setting,
		float settled_speed_kmh,
		float settled_base_speed,
		float& out_normal_degrees_per_second,
		float& out_drift_degrees_per_second,
		bool& out_drift_observed) {
	out_normal_degrees_per_second = 0.0f;
	out_drift_degrees_per_second = 0.0f;
	out_drift_observed = false;
	if (car_properties.is_empty() || !std::isfinite(machine_setting) ||
			!std::isfinite(settled_speed_kmh) || !std::isfinite(settled_base_speed)) {
		return false;
	}

	Ref<StreamPeerBuffer> track_buffer = make_flat_benchmark_track();
	String validation_error;
	if (!mxt::content::validate_track_payload(track_buffer->get_data_array(), validation_error)) {
		return false;
	}

	GameSim *benchmark = memnew(GameSim);
	benchmark->performance_benchmark_mode = true;
	benchmark->bumpers_enabled = false;
	benchmark->s_boost_enabled = false;
	benchmark->vehicle_restore_enabled = false;

	Array property_buffers;
	Array machine_settings;
	for (int car = 0; car < BENCHMARK_CAR_COUNT; ++car) {
		property_buffers.push_back(car_properties);
		machine_settings.push_back(static_cast<double>(machine_setting));
	}
	benchmark->instantiate_gamesim(track_buffer.ptr(), property_buffers, machine_settings);
	if (benchmark->num_cars != BENCHMARK_CAR_COUNT || !benchmark->cars) {
		memdelete(benchmark);
		return false;
	}

	PlayerInput inputs[BENCHMARK_CAR_COUNT] = {
		PlayerInput::from_neutral(), PlayerInput::from_neutral()};
	benchmark->tick = 180;
	for (int car = 0; car < BENCHMARK_CAR_COUNT; ++car) {
		PhysicsCarSoA &soa = *benchmark->cars[car].soa;
		const int lane = benchmark->cars[car].soa_index;
		place_car_on_benchmark_road(
			benchmark->cars[car], car == 0 ? -600.0f : 600.0f);
		soa.level_start_time[lane] = benchmark->tick;
		soa.machine_state[lane] |= MACHINESTATE::ACTIVE;
		soa.machine_state[lane] &= ~MACHINESTATE::STARTINGCOUNTDOWN;
		inputs[car].accelerate = 1.0f;
	}

	for (int frame = 0; frame < SETTLE_FRAMES; ++frame) {
		benchmark->tick_gamesim_internal(InputFrameMode::DecodedCarArray,
			-1, nullptr, inputs, nullptr, BENCHMARK_CAR_COUNT,
			nullptr, nullptr, false);
	}

	const float start_speed_kmh = std::max(settled_speed_kmh, 0.0f);
	const float start_base_speed = std::max(settled_base_speed, 0.0f);
	for (int car = 0; car < BENCHMARK_CAR_COUNT; ++car) {
		PhysicsCar &car_view = benchmark->cars[car];
		PhysicsCarSoA &soa = *car_view.soa;
		const int lane = car_view.soa_index;
		const SimVec3 forward = flat_forward(car_view);
		const float world_speed = start_speed_kmh *
			std::max(std::abs(soa.stat_weight[lane]), 1.0f) / 216.0f;
		STORE_INDEXED_VEC3(soa, velocity, lane, forward * world_speed);
		STORE_INDEXED_VEC3(soa, knockback_velocity, lane, SimVec3());
		STORE_INDEXED_VEC3(soa, velocity_angular, lane, SimVec3());
		soa.base_speed[lane] = start_base_speed;
		soa.grip_frames_from_accel_press[lane] = 0;
	}

	inputs[0].steer_horizontal = 1.0f;
	inputs[1].steer_horizontal = 1.0f;
	inputs[1].strafe_left = 1.0f;
	inputs[1].strafe_right = 1.0f;

	bool drift_observed = false;
	for (int frame = 0; frame < TURN_WARMUP_FRAMES; ++frame) {
		benchmark->tick_gamesim_internal(InputFrameMode::DecodedCarArray,
			-1, nullptr, inputs, nullptr, BENCHMARK_CAR_COUNT,
			nullptr, nullptr, false);
		drift_observed = drift_observed || car_is_drifting(benchmark->cars[1]);
	}

	SimVec3 previous_forward[BENCHMARK_CAR_COUNT] = {
		flat_forward(benchmark->cars[0]), flat_forward(benchmark->cars[1])};
	float accumulated_angle[BENCHMARK_CAR_COUNT] = {};
	for (int frame = 0; frame < TURN_MEASURE_FRAMES; ++frame) {
		benchmark->tick_gamesim_internal(InputFrameMode::DecodedCarArray,
			-1, nullptr, inputs, nullptr, BENCHMARK_CAR_COUNT,
			nullptr, nullptr, false);
		for (int car = 0; car < BENCHMARK_CAR_COUNT; ++car) {
			const SimVec3 current_forward = flat_forward(benchmark->cars[car]);
			accumulated_angle[car] += signed_flat_angle(previous_forward[car], current_forward);
			previous_forward[car] = current_forward;
		}
		drift_observed = drift_observed || car_is_drifting(benchmark->cars[1]);
	}

	const float degrees_per_second_scale =
		RADIANS_TO_DEGREES * 60.0f / static_cast<float>(TURN_MEASURE_FRAMES);
	out_normal_degrees_per_second = std::abs(accumulated_angle[0]) * degrees_per_second_scale;
	out_drift_degrees_per_second = std::abs(accumulated_angle[1]) * degrees_per_second_scale;
	out_drift_observed = drift_observed;
	const bool valid = std::isfinite(out_normal_degrees_per_second) &&
		std::isfinite(out_drift_degrees_per_second);
	memdelete(benchmark);
	return valid;
}

} // namespace godot
