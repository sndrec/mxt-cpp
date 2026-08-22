#include "gamesim/gamesim_internal.h"

#include "godot_cpp/classes/engine.hpp"
#include "godot_cpp/classes/time.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

using namespace godot;

namespace godot {

static constexpr int COLLISION_SPARK_CAPACITY = 512;
static constexpr int COLLISION_SPARK_BUFFER_STRIDE = 20;
static constexpr float COLLISION_SPARK_PERSISTENCE_MSEC = 40.0f;
static constexpr float COLLISION_SPARK_SIZE = 0.025f;
static constexpr float COLLISION_SPARK_GRAVITY = 0.008f;
static constexpr float COLLISION_SPARK_DAMPING = 0.992f;
static constexpr float COLLISION_SPARK_BOUNCE = 0.5f;
static constexpr float COLLISION_SPARK_PLANE_OFFSET = 0.025f;
static constexpr float IMPACT_FLASH_DECAY_PER_TICK = 0.76f;

struct CollisionSparkParticle {
	SimVec3 position;
	SimVec3 previous_position;
	SimVec3 velocity;
	SimVec3 gravity_direction;
	SimVec3 bounce_plane_point;
	SimVec3 bounce_plane_normal;
	float color_r = 1.0f;
	float color_g = 0.4f;
	float color_b = 0.1f;
	uint32_t generation = 0;
	uint16_t life = 0;
	uint16_t total_life = 1;
	uint8_t active = 0;
};

struct CollisionSparkScreenHistory {
	Vector3 older_view;
	Vector3 previous_view;
	double last_update_msec = 0.0;
	uint64_t last_seen_frame = 0;
	uint32_t generation = 0;
};

struct CollisionSparkEmitterState {
	float ground_scrape_accumulator = 0.0f;
	uint32_t last_rail_hit_tick = 0;
	uint32_t last_machine_hit_tick = 0;
	uint32_t previous_machine_state = 0;
	uint8_t rail_hit_initialized = 0;
	uint8_t machine_hit_initialized = 0;
};

struct CollisionSparkRuntime {
	std::array<CollisionSparkParticle, COLLISION_SPARK_CAPACITY> particles;
	std::array<CollisionSparkScreenHistory, COLLISION_SPARK_CAPACITY> screen_history;
	std::vector<CollisionSparkEmitterState> emitters;
	PackedFloat32Array render_buffer;
	MultiMeshInstance3D* multimesh_instance = nullptr;
	uint32_t rng_state = 0x6d2b79f5u;
	uint32_t next_generation = 1;
	uint16_t cursor = 0;
	uint64_t render_frame = 0;
	int last_sim_tick = -1;
};

static uint32_t collision_spark_rand_u32(uint32_t& state)
{
	if (state == 0) {
		state = 0x6d2b79f5u;
	}
	state ^= state << 13;
	state ^= state >> 17;
	state ^= state << 5;
	return state;
}

static float collision_spark_rand01(uint32_t& state)
{
	return static_cast<float>(collision_spark_rand_u32(state) >> 8) * (1.0f / 16777216.0f);
}

static float collision_spark_rand_range(uint32_t& state, float min_value, float max_value)
{
	return min_value + (max_value - min_value) * collision_spark_rand01(state);
}

static SimVec3 normalized_or(const SimVec3& value, const SimVec3& fallback)
{
	if (value.length_squared() <= 0.000001f) {
		return fallback;
	}
	return value.normalized();
}

static SimVec3 collision_spark_contact_corner(PhysicsCarSoA& soa, int lane, const SimVec3& outward_normal)
{
	const SimTransform basis = MXT_LOAD_TRANSFORM(soa, basis_physical, lane);
	const SimVec3 origin = LOAD_INDEXED_VEC3(soa, position_current, lane);
	const int point_base = lane * 4;
	SimVec3 best = origin;
	float best_projection = INFINITY;
	for (int point = 0; point < 4; ++point) {
		const int offset_index = point_base + point;
		const SimVec3 offset(
			soa.wall_offset_x[offset_index],
			soa.wall_offset_y[offset_index],
			soa.wall_offset_z[offset_index]);
		const SimVec3 candidate = origin + basis.basis.xform(offset);
		const float projection = candidate.dot(outward_normal);
		if (projection < best_projection) {
			best_projection = projection;
			best = candidate;
		}
	}
	return best;
}

static void collision_spark_spawn(
	CollisionSparkRuntime& runtime,
	const SimVec3& position,
	const SimVec3& source_velocity,
	const SimVec3& outward_normal,
	const SimVec3& gravity_up,
	const SimVec3& bounce_plane_point,
	const SimVec3& bounce_plane_normal,
	float normal_velocity_scale)
{
	CollisionSparkParticle& spark = runtime.particles[runtime.cursor];
	runtime.cursor = static_cast<uint16_t>((runtime.cursor + 1) % COLLISION_SPARK_CAPACITY);
	spark = CollisionSparkParticle();
	spark.active = 1;
	spark.generation = runtime.next_generation++;
	if (runtime.next_generation == 0) {
		runtime.next_generation = 1;
	}
	spark.position = position;
	spark.previous_position = position;
	const float speed = source_velocity.length();
	const float spread = speed * 0.15f;
	spark.velocity = source_velocity * collision_spark_rand_range(runtime.rng_state, 0.9f, 1.05f);
	spark.velocity += outward_normal * (speed * normal_velocity_scale);
	spark.velocity += gravity_up * (speed * collision_spark_rand_range(runtime.rng_state, 0.0f, 0.2f));
	spark.velocity += SimVec3(
		collision_spark_rand_range(runtime.rng_state, -spread, spread),
		collision_spark_rand_range(runtime.rng_state, -spread, spread),
		collision_spark_rand_range(runtime.rng_state, -spread, spread));
	spark.gravity_direction = gravity_up * -1.0f;
	spark.bounce_plane_point = bounce_plane_point;
	spark.bounce_plane_normal = bounce_plane_normal;
	spark.color_r = collision_spark_rand_range(runtime.rng_state, 1.0f, 3.0f) * 3.0f;
	spark.color_g = collision_spark_rand_range(runtime.rng_state, 0.0f, 2.0f) * 3.0f;
	spark.color_b = collision_spark_rand_range(runtime.rng_state, 0.0f, 0.5f) * 3.0f;
	spark.total_life = static_cast<uint16_t>(15 + (collision_spark_rand_u32(runtime.rng_state) % 16));
	spark.life = spark.total_life;
}

static void collision_spark_step_particles(CollisionSparkRuntime& runtime)
{
	for (CollisionSparkParticle& spark : runtime.particles) {
		if (!spark.active) {
			continue;
		}
		if (spark.life == 0) {
			spark.active = 0;
			continue;
		}
		spark.previous_position = spark.position;
		spark.velocity = (spark.velocity + spark.gravity_direction * COLLISION_SPARK_GRAVITY) * COLLISION_SPARK_DAMPING;
		spark.position += spark.velocity;
		const float plane_distance =
			(spark.position - spark.bounce_plane_point).dot(spark.bounce_plane_normal);
		const float normal_velocity = spark.velocity.dot(spark.bounce_plane_normal);
		if (plane_distance < COLLISION_SPARK_PLANE_OFFSET && normal_velocity < 0.0f) {
			spark.position += spark.bounce_plane_normal * (COLLISION_SPARK_PLANE_OFFSET - plane_distance);
			spark.velocity -= spark.bounce_plane_normal * (normal_velocity * (1.0f + COLLISION_SPARK_BOUNCE));
		}
		--spark.life;
		if (spark.life == 0) {
			spark.active = 0;
		}
	}
}

static void collision_spark_emit_burst(
	CollisionSparkRuntime& runtime,
	PhysicsCarSoA& soa,
	int lane,
	const SimVec3& position,
	const SimVec3& outward_normal,
	int count,
	float normal_velocity_scale)
{
	const float inverse_weight = 1.0f / std::max(soa.stat_weight[lane], 0.001f);
	const SimVec3 velocity = LOAD_INDEXED_VEC3(soa, velocity, lane) * inverse_weight;
	const SimVec3 track_up = normalized_or(
		LOAD_INDEXED_VEC3(soa, track_surface_normal, lane),
		SimVec3(0.0f, 1.0f, 0.0f));
	const SimVec3 plane_point = LOAD_INDEXED_VEC3(soa, track_surface_pos, lane);
	for (int spark_index = 0; spark_index < count; ++spark_index) {
		collision_spark_spawn(
			runtime,
			position,
			velocity,
			outward_normal,
			track_up,
			plane_point,
			track_up,
			normal_velocity_scale);
	}
}

static void collision_spark_reset_history(CollisionSparkRuntime& runtime)
{
	for (CollisionSparkParticle& spark : runtime.particles) {
		spark.active = 0;
	}
	for (CollisionSparkScreenHistory& history : runtime.screen_history) {
		history = CollisionSparkScreenHistory();
	}
	runtime.cursor = 0;
	runtime.render_frame = 0;
}

void GameSim::step_collision_spark_effects()
{
	if (!cars || num_cars <= 0) {
		return;
	}
	if (!collision_spark_runtime) {
		collision_spark_runtime = new CollisionSparkRuntime();
	}
	CollisionSparkRuntime& runtime = *collision_spark_runtime;
	if (static_cast<int>(runtime.emitters.size()) != num_cars) {
		runtime.emitters.assign(num_cars, CollisionSparkEmitterState());
		collision_spark_reset_history(runtime);
		runtime.last_sim_tick = -1;
	}
	if (runtime.last_sim_tick < 0 || tick <= runtime.last_sim_tick || tick - runtime.last_sim_tick > 8) {
		collision_spark_reset_history(runtime);
		runtime.last_sim_tick = tick;
		for (int car_index = 0; car_index < num_cars; ++car_index) {
			PhysicsCarSoA& soa = *cars[car_index].soa;
			const int lane = cars[car_index].soa_index;
			CollisionSparkEmitterState& emitter = runtime.emitters[car_index];
			emitter.ground_scrape_accumulator = 0.0f;
			emitter.previous_machine_state = soa.machine_state[lane];
			if (soa.has_last_hit_tick[lane]) {
				emitter.last_rail_hit_tick = soa.last_hit_tick[lane];
				emitter.rail_hit_initialized = 1;
			}
			if (soa.has_last_machine_hit_tick[lane]) {
				emitter.last_machine_hit_tick = soa.last_machine_hit_tick[lane];
				emitter.machine_hit_initialized = 1;
			}
		}
		return;
	}

	for (int skipped_tick = runtime.last_sim_tick; skipped_tick < tick; ++skipped_tick) {
		collision_spark_step_particles(runtime);
		for (RenderVehicleEffectRefs& refs : render_vehicle_effect_refs) {
			refs.impact_flash *= IMPACT_FLASH_DECAY_PER_TICK;
			if (refs.impact_flash < 0.001f) {
				refs.impact_flash = 0.0f;
			}
		}
	}
	runtime.last_sim_tick = tick;

	for (int car_index = 0; car_index < num_cars; ++car_index) {
		PhysicsCarSoA& soa = *cars[car_index].soa;
		const int lane = cars[car_index].soa_index;
		CollisionSparkEmitterState& emitter = runtime.emitters[car_index];
		const uint32_t machine_state = soa.machine_state[lane];
		const SimVec3 track_up = normalized_or(
			LOAD_INDEXED_VEC3(soa, track_surface_normal, lane),
			SimVec3(0.0f, 1.0f, 0.0f));

		bool machine_hit =
			(machine_state & MACHINESTATE::JUSTHITVEHICLE_Q) != 0u &&
			(emitter.previous_machine_state & MACHINESTATE::JUSTHITVEHICLE_Q) == 0u;
		if (soa.has_last_machine_hit_tick[lane]) {
			if (emitter.machine_hit_initialized &&
				soa.last_machine_hit_tick[lane] != emitter.last_machine_hit_tick) {
				machine_hit = true;
			}
			emitter.last_machine_hit_tick = soa.last_machine_hit_tick[lane];
			emitter.machine_hit_initialized = 1;
		}
		if (machine_hit) {
			const SimVec3 response = LOAD_INDEXED_VEC3(soa, collision_response, lane);
			const SimVec3 outward = normalized_or(response * -1.0f, track_up);
			const SimVec3 origin = LOAD_INDEXED_VEC3(soa, position_current, lane);
			const float strength = std::clamp(
				soa.has_last_machine_hit_tick[lane] ? soa.last_machine_hit_sfx_strength[lane] : response.length() * 0.1f,
				0.1f,
				1.0f);
			const int count = std::clamp(2 + static_cast<int>(std::ceil(strength * 8.0f)), 2, 12);
			collision_spark_emit_burst(runtime, soa, lane, origin + outward * 1.5f, outward, count, 0.12f);
			if (car_index < static_cast<int>(render_vehicle_effect_refs.size())) {
				render_vehicle_effect_refs[car_index].impact_flash = std::max(
					render_vehicle_effect_refs[car_index].impact_flash,
					0.35f + strength * 1.25f);
			}
		}

		bool rail_hit = false;
		if (soa.has_last_hit_tick[lane]) {
			if (emitter.rail_hit_initialized && soa.last_hit_tick[lane] != emitter.last_rail_hit_tick) {
				rail_hit = true;
			}
			emitter.last_rail_hit_tick = soa.last_hit_tick[lane];
			emitter.rail_hit_initialized = 1;
		}
		if (rail_hit) {
			const SimVec3 rail_push = LOAD_INDEXED_VEC3(soa, collision_push_rail, lane);
			const SimVec3 outward = normalized_or(rail_push, track_up);
			const SimVec3 position = collision_spark_contact_corner(soa, lane, outward) + outward * 0.01f;
			const float strength = std::clamp(soa.last_hit_sfx_strength[lane], 0.1f, 1.0f);
			const int count = std::clamp(2 + static_cast<int>(std::ceil(strength * 8.0f)), 2, 12);
			collision_spark_emit_burst(runtime, soa, lane, position, outward, count, 0.2f);
			if (car_index < static_cast<int>(render_vehicle_effect_refs.size())) {
				render_vehicle_effect_refs[car_index].impact_flash = std::max(
					render_vehicle_effect_refs[car_index].impact_flash,
					0.35f + strength * 1.25f);
			}
		}

		const SimVec3 track_push = LOAD_INDEXED_VEC3(soa, collision_push_track, lane);
		const float track_push_length = track_push.length();
		const float speed = LOAD_INDEXED_VEC3(soa, velocity, lane).length();
		if (track_push_length > 0.0023148148f && speed > 0.25f) {
			const float scrape_rate = std::clamp((track_push_length + 0.02f) * speed * 0.264f, 0.0f, 2.0f);
			emitter.ground_scrape_accumulator += scrape_rate;
			const SimVec3 position = collision_spark_contact_corner(soa, lane, track_up) + track_up * 0.01f;
			int emitted = 0;
			while (emitter.ground_scrape_accumulator >= 1.0f && emitted < 2) {
				collision_spark_emit_burst(runtime, soa, lane, position, track_up, 1, 0.05f);
				emitter.ground_scrape_accumulator -= 1.0f;
				++emitted;
			}
		} else {
			emitter.ground_scrape_accumulator = std::min(emitter.ground_scrape_accumulator, 0.99f);
		}
		emitter.previous_machine_state = machine_state;
	}
}

static Vector3 collision_spark_resolve_tail(
	CollisionSparkScreenHistory& history,
	const Vector3& head_view,
	uint32_t generation,
	uint64_t render_frame,
	double now_msec)
{
	const bool reset =
		history.generation != generation ||
		history.last_seen_frame + 1 < render_frame ||
		history.last_update_msec <= 0.0;
	if (reset) {
		history.older_view = head_view;
		history.previous_view = head_view;
		history.last_update_msec = now_msec;
		history.generation = generation;
	}
	if (now_msec > history.last_update_msec + COLLISION_SPARK_PERSISTENCE_MSEC) {
		const int steps = std::min(
			4,
			static_cast<int>(std::floor(
				(now_msec - history.last_update_msec) / COLLISION_SPARK_PERSISTENCE_MSEC)));
		for (int step = 0; step < steps; ++step) {
			history.older_view = history.previous_view;
			history.previous_view = head_view;
			history.last_update_msec += COLLISION_SPARK_PERSISTENCE_MSEC;
		}
	}
	const float ratio = static_cast<float>(std::clamp(
		(now_msec - history.last_update_msec) / COLLISION_SPARK_PERSISTENCE_MSEC,
		0.0,
		1.0));
	history.last_seen_frame = render_frame;
	return history.older_view.lerp(history.previous_view, ratio);
}

void GameSim::render_collision_spark_effects(float alpha)
{
	if (!collision_spark_runtime || !spark_node_container) {
		return;
	}
	CollisionSparkRuntime& runtime = *collision_spark_runtime;
	if (!runtime.multimesh_instance) {
		Node* node = spark_node_container->get_node_or_null(NodePath("CollisionSparkMultiMesh"));
		runtime.multimesh_instance = Object::cast_to<MultiMeshInstance3D>(node);
		if (!runtime.multimesh_instance) {
			return;
		}
	}
	Ref<MultiMesh> multimesh = runtime.multimesh_instance->get_multimesh();
	if (multimesh.is_null()) {
		return;
	}
	Camera3D* camera = render_camera_node ? render_camera_node : gameplay_camera_node;
	if (!camera) {
		multimesh->set_visible_instance_count(0);
		return;
	}
	const int required_buffer_size = multimesh->get_instance_count() * COLLISION_SPARK_BUFFER_STRIDE;
	if (runtime.render_buffer.size() != required_buffer_size) {
		runtime.render_buffer.resize(required_buffer_size);
	}
	float* buffer = runtime.render_buffer.ptrw();
	const Transform3D view_from_world = camera->get_global_transform().affine_inverse();
	const double now_msec = static_cast<double>(Time::get_singleton()->get_ticks_usec()) / 1000.0;
	runtime.render_frame += 1;
	int visible_count = 0;
	for (int particle_index = 0; particle_index < COLLISION_SPARK_CAPACITY; ++particle_index) {
		const CollisionSparkParticle& spark = runtime.particles[particle_index];
		// A spark is spawned at the current 60 Hz simulation position while the
		// vehicle is still rendered between its previous and current positions.
		// Keep that incomplete newborn interval hidden; after one simulation step,
		// both can be interpolated across the same pair of ticks.
		if (!spark.active || spark.life == spark.total_life) {
			continue;
		}
		const SimVec3 interpolated_position = spark.previous_position.lerp(spark.position, alpha);
		const Vector3 head_view = view_from_world.xform(gd_vec3(interpolated_position));
		CollisionSparkScreenHistory& history = runtime.screen_history[particle_index];
		const Vector3 tail_view = collision_spark_resolve_tail(
			history,
			head_view,
			spark.generation,
			runtime.render_frame,
			now_msec);
		if (head_view.z >= -0.001f || tail_view.z >= -0.001f) {
			continue;
		}
		float* instance = buffer + visible_count * COLLISION_SPARK_BUFFER_STRIDE;
		for (int component = 0; component < COLLISION_SPARK_BUFFER_STRIDE; ++component) {
			instance[component] = 0.0f;
		}
		// Basis column zero and origin are deliberately data channels consumed by
		// collision_spark.gdshader; the quad never uses this as a world transform.
		instance[0] = tail_view.x;
		instance[4] = tail_view.y;
		instance[8] = tail_view.z;
		instance[5] = 1.0f;
		instance[10] = 1.0f;
		instance[3] = head_view.x;
		instance[7] = head_view.y;
		instance[11] = head_view.z;
		const float life_ratio = static_cast<float>(spark.life) / static_cast<float>(std::max<uint16_t>(1, spark.total_life));
		instance[12] = spark.color_r;
		instance[13] = spark.color_g;
		instance[14] = spark.color_b;
		instance[15] = 1.0f;
		instance[16] = COLLISION_SPARK_SIZE * life_ratio;
		++visible_count;
	}
	if (visible_count > 0) {
		multimesh->set_buffer(runtime.render_buffer);
	}
	multimesh->set_visible_instance_count(visible_count);
}

void GameSim::reset_collision_spark_effects(bool release_render_node)
{
	if (!collision_spark_runtime) {
		return;
	}
	CollisionSparkRuntime& runtime = *collision_spark_runtime;
	collision_spark_reset_history(runtime);
	runtime.emitters.clear();
	runtime.last_sim_tick = -1;
	for (RenderVehicleEffectRefs& refs : render_vehicle_effect_refs) {
		refs.impact_flash = 0.0f;
	}
	if (runtime.multimesh_instance) {
		Ref<MultiMesh> multimesh = runtime.multimesh_instance->get_multimesh();
		if (multimesh.is_valid()) {
			multimesh->set_visible_instance_count(0);
		}
	}
	if (release_render_node) {
		runtime.multimesh_instance = nullptr;
		runtime.render_buffer.clear();
	}
}

void GameSim::destroy_collision_spark_runtime()
{
	if (!collision_spark_runtime) {
		return;
	}
	reset_collision_spark_effects(true);
	delete collision_spark_runtime;
	collision_spark_runtime = nullptr;
}

} // namespace godot
