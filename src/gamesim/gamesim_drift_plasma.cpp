#include "gamesim/gamesim_internal.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

using namespace godot;

namespace godot {

// F-Zero GX's ET_DRIFT_PTCL pool is fixed-capacity and emits short textured
// ribbons from grounded, drifting tilt corners. The source callbacks are
// GFZE01 fn_1_59514/fn_1_5962C/fn_1_59770 and the emitter is fn_1_6AF70.
static constexpr int DRIFT_PLASMA_CAPACITY = 1536;
static constexpr int DRIFT_PLASMA_BUFFER_STRIDE = 20;
static constexpr int DRIFT_PLASMA_REMOTE_EMISSION_INTERVAL = 5;
static constexpr float DRIFT_PLASMA_LATERAL_THRESHOLD = 1.0f;
static constexpr float DRIFT_PLASMA_CORNER_MOTION_SCALE = 0.93f;
static constexpr float DRIFT_PLASMA_RANDOM_MOTION = 0.4f;
static constexpr float DRIFT_PLASMA_NORMAL_SPEED = 0.8f;
static constexpr float DRIFT_PLASMA_GRAVITY_Y = -0.03f;
static constexpr float DRIFT_PLASMA_DAMPING_EASE = 0.05f;
static constexpr float DRIFT_PLASMA_WIDTH_EASE = 0.1f;

struct DriftPlasmaParticle {
	SimVec3 older_position;
	SimVec3 previous_position;
	SimVec3 position;
	SimVec3 velocity;
	float width = 0.2f;
	float target_width = 0.3f;
	float render_width_scale = 1.0f;
	float damping = 0.0f;
	float target_damping = 0.0f;
	float red = 0.0f;
	float green = 0.0f;
	float blue = 0.0f;
	uint16_t life = 0;
	uint8_t active = 0;
};

struct DriftPlasmaRuntime {
	std::array<DriftPlasmaParticle, DRIFT_PLASMA_CAPACITY> particles;
	PackedFloat32Array render_buffer;
	MultiMeshInstance3D* multimesh_instance = nullptr;
	uint32_t rng_state = 0x47d15eedu;
	uint16_t cursor = 0;
	int last_sim_tick = -1;
};

static uint32_t drift_plasma_rand_u32(uint32_t& state)
{
	if (state == 0) {
		state = 0x47d15eedu;
	}
	state ^= state << 13;
	state ^= state >> 17;
	state ^= state << 5;
	return state;
}

static float drift_plasma_rand01(uint32_t& state)
{
	return static_cast<float>(drift_plasma_rand_u32(state) >> 8) * (1.0f / 16777216.0f);
}

static float drift_plasma_rand_range(uint32_t& state, float minimum, float maximum)
{
	return minimum + (maximum - minimum) * drift_plasma_rand01(state);
}

static SimVec3 drift_plasma_normalized_or(const SimVec3& value, const SimVec3& fallback)
{
	if (value.length_squared() <= 0.000001f) {
		return fallback;
	}
	return value.normalized();
}

static bool drift_plasma_frame(
	const PhysicsCarSoA& soa,
	int lane,
	SimVec3& out_lateral,
	SimVec3& out_up)
{
	const SimVec3 forward(
		-soa.basis_physical_c2x[lane],
		-soa.basis_physical_c2y[lane],
		-soa.basis_physical_c2z[lane]);
	const SimVec3 physical_up(
		soa.basis_physical_c1x[lane],
		soa.basis_physical_c1y[lane],
		soa.basis_physical_c1z[lane]);
	out_up = drift_plasma_normalized_or(
		LOAD_INDEXED_VEC3(soa, track_surface_normal, lane),
		drift_plasma_normalized_or(physical_up, SimVec3(0.0f, 1.0f, 0.0f)));
	out_lateral = SimVec3(
		-(forward.y * out_up.z - forward.z * out_up.y),
		-(forward.z * out_up.x - forward.x * out_up.z),
		-(forward.x * out_up.y - forward.y * out_up.x));
	if (out_lateral.length_squared() <= 0.000001f) {
		return false;
	}
	out_lateral = out_lateral.normalized();
	return true;
}

static void drift_plasma_advance_particle(DriftPlasmaParticle& particle)
{
	--particle.life;
	if (particle.life == 0u) {
		particle.active = 0;
		return;
	}
	particle.older_position = particle.previous_position;
	particle.previous_position = particle.position;
	particle.velocity.y += DRIFT_PLASMA_GRAVITY_Y;
	particle.velocity *= 1.0f - particle.damping;
	particle.position += particle.velocity;
	particle.damping += DRIFT_PLASMA_DAMPING_EASE * (particle.target_damping - particle.damping);
	particle.width += DRIFT_PLASMA_WIDTH_EASE * (particle.target_width - particle.width);
	if (particle.life < 15u) {
		const float fade = 1.0f - 1.0f / static_cast<float>(particle.life + 1u);
		particle.red *= fade;
		particle.green *= fade;
		particle.blue *= fade;
	}
}

static void drift_plasma_spawn(
	DriftPlasmaRuntime& runtime,
	const SimVec3& corner_position,
	const SimVec3& corner_delta,
	const SimVec3& lateral,
	const SimVec3& corner_normal,
	float side_sign,
	float intensity,
	float speed_kmh,
	bool half_render_width)
{
	DriftPlasmaParticle& particle = runtime.particles[runtime.cursor];
	runtime.cursor = static_cast<uint16_t>((runtime.cursor + 1) % DRIFT_PLASMA_CAPACITY);
	particle = DriftPlasmaParticle();
	particle.active = 1;

	particle.velocity = corner_delta * DRIFT_PLASMA_CORNER_MOTION_SCALE;
	particle.velocity -= corner_normal * particle.velocity.dot(corner_normal);
	particle.velocity += SimVec3(
		drift_plasma_rand_range(runtime.rng_state, -DRIFT_PLASMA_RANDOM_MOTION, DRIFT_PLASMA_RANDOM_MOTION),
		drift_plasma_rand_range(runtime.rng_state, -DRIFT_PLASMA_RANDOM_MOTION, DRIFT_PLASMA_RANDOM_MOTION),
		drift_plasma_rand_range(runtime.rng_state, -DRIFT_PLASMA_RANDOM_MOTION, DRIFT_PLASMA_RANDOM_MOTION));
	particle.velocity += corner_normal * (DRIFT_PLASMA_NORMAL_SPEED * intensity);
	particle.velocity += lateral * (side_sign * intensity);

	const SimVec3 position_jitter(
		drift_plasma_rand_range(runtime.rng_state, -DRIFT_PLASMA_RANDOM_MOTION, DRIFT_PLASMA_RANDOM_MOTION),
		drift_plasma_rand_range(runtime.rng_state, -DRIFT_PLASMA_RANDOM_MOTION, DRIFT_PLASMA_RANDOM_MOTION),
		drift_plasma_rand_range(runtime.rng_state, -DRIFT_PLASMA_RANDOM_MOTION, DRIFT_PLASMA_RANDOM_MOTION));
	particle.position = corner_position + position_jitter - particle.velocity;
	particle.previous_position = particle.position;
	particle.older_position = particle.position;

	particle.target_width = 0.5f * (0.5f + 0.3f * drift_plasma_rand01(runtime.rng_state));
	particle.width = 0.5f * (0.4f + 0.2f * intensity * drift_plasma_rand01(runtime.rng_state));
	particle.render_width_scale = half_render_width ? 0.5f : 1.0f;
	particle.damping = 0.05f * drift_plasma_rand01(runtime.rng_state);
	particle.target_damping = 0.07f + std::max(0.0f, speed_kmh) * (1.0f / 20000.0f);
	const float color_scale = 0.5f + 0.3f * drift_plasma_rand01(runtime.rng_state);
	particle.red = 0.8f * color_scale;
	particle.green = 0.5f * color_scale;
	particle.blue = particle.green;
	particle.life = static_cast<uint16_t>(60.0f *
		(0.05f + 0.08f * intensity * drift_plasma_rand01(runtime.rng_state)));
	particle.life = std::max<uint16_t>(particle.life, 1u);

	// GX updates a freshly allocated effect before the particle ribbon is
	// submitted, so the first visible segment already moves away from the corner.
	drift_plasma_advance_particle(particle);
}

static void drift_plasma_step_particles(DriftPlasmaRuntime& runtime)
{
	for (DriftPlasmaParticle& particle : runtime.particles) {
		if (!particle.active) {
			continue;
		}
		if (particle.life == 0) {
			particle.active = 0;
			continue;
		}
		drift_plasma_advance_particle(particle);
	}
}

static void drift_plasma_clear(DriftPlasmaRuntime& runtime)
{
	for (DriftPlasmaParticle& particle : runtime.particles) {
		particle.active = 0;
	}
	runtime.cursor = 0;
}

void GameSim::step_drift_plasma_effects()
{
	if (!cars || num_cars <= 0) {
		return;
	}
	if (!drift_plasma_runtime) {
		drift_plasma_runtime = new DriftPlasmaRuntime();
	}
	DriftPlasmaRuntime& runtime = *drift_plasma_runtime;
	if (runtime.last_sim_tick < 0 || tick < runtime.last_sim_tick || tick - runtime.last_sim_tick > 8) {
		drift_plasma_clear(runtime);
		runtime.last_sim_tick = tick;
		return;
	}
	if (tick == runtime.last_sim_tick) {
		return;
	}
	for (int skipped_tick = runtime.last_sim_tick; skipped_tick < tick; ++skipped_tick) {
		drift_plasma_step_particles(runtime);
	}
	runtime.last_sim_tick = tick;

	for (int car_index = 0; car_index < num_cars; ++car_index) {
		const bool focused_car =
			car_player_ids && car_player_ids[car_index] == gameplay_camera_player_id;
		if (!focused_car && tick % DRIFT_PLASMA_REMOTE_EMISSION_INTERVAL != 0) {
			continue;
		}

		PhysicsCarSoA& soa = *cars[car_index].soa;
		const int lane = cars[car_index].soa_index;
		if ((soa.machine_state[lane] & (MACHINESTATE::ZEROHP | MACHINESTATE::FALLOUT)) != 0u) {
			continue;
		}
		SimVec3 lateral;
		SimVec3 up;
		if (!drift_plasma_frame(soa, lane, lateral, up)) {
			continue;
		}

		const int point_base = lane * 4;
		for (int corner = 0; corner < 4; ++corner) {
			const int point = point_base + corner;
			const uint32_t state = soa.tilt_state[point];
			if ((state & TILTSTATE::DRIFT) == 0u || (state & TILTSTATE::AIRBORNE) != 0u) {
				continue;
			}
			const SimVec3 position(
				soa.tilt_pos_x[point],
				soa.tilt_pos_y[point],
				soa.tilt_pos_z[point]);
			const SimVec3 previous_position(
				soa.tilt_pos_old_x[point],
				soa.tilt_pos_old_y[point],
				soa.tilt_pos_old_z[point]);
			const SimVec3 corner_delta = position - previous_position;
			const float lateral_delta = corner_delta.dot(lateral);
			const bool positive_side_corner = corner == 0 || corner == 2;
			const float side_sign = positive_side_corner ? 1.0f : -1.0f;
			const float same_side_delta = lateral_delta * side_sign;
			const float intensity = std::clamp(
				same_side_delta - DRIFT_PLASMA_LATERAL_THRESHOLD,
				0.0f,
				1.0f);
			if (intensity <= 0.0f) {
				continue;
			}
			const SimVec3 corner_normal = drift_plasma_normalized_or(
				SimVec3(
					soa.tilt_up_vector_x[point],
					soa.tilt_up_vector_y[point],
					soa.tilt_up_vector_z[point]),
				up);

			const float corner_speed = std::min(corner_delta.length(), 5.0f);
			const int particle_count = corner_speed <= 2.0f ? 1 : (corner_speed <= 4.0f ? 2 : 3);
			for (int particle_index = 0; particle_index < particle_count; ++particle_index) {
				drift_plasma_spawn(
					runtime,
					position,
					corner_delta,
					lateral,
					corner_normal,
					side_sign,
					intensity,
					soa.speed_kmh[lane],
					corner < 2);
			}
		}
	}
}

void GameSim::render_drift_plasma_effects(float alpha)
{
	if (!drift_plasma_runtime || !spark_node_container) {
		return;
	}
	DriftPlasmaRuntime& runtime = *drift_plasma_runtime;
	if (!runtime.multimesh_instance) {
		Node* node = spark_node_container->get_node_or_null(NodePath("DriftPlasmaMultiMesh"));
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

	const int required_buffer_size = multimesh->get_instance_count() * DRIFT_PLASMA_BUFFER_STRIDE;
	if (runtime.render_buffer.size() != required_buffer_size) {
		runtime.render_buffer.resize(required_buffer_size);
	}
	float* buffer = runtime.render_buffer.ptrw();
	const Transform3D view_from_world = camera->get_global_transform().affine_inverse();
	int visible_count = 0;
	for (const DriftPlasmaParticle& particle : runtime.particles) {
		if (!particle.active) {
			continue;
		}
		const SimVec3 tail_position = particle.older_position.lerp(particle.previous_position, alpha);
		const SimVec3 head_position = particle.previous_position.lerp(particle.position, alpha);
		const Vector3 tail_view = view_from_world.xform(gd_vec3(tail_position));
		const Vector3 head_view = view_from_world.xform(gd_vec3(head_position));
		if (head_view.z >= -0.001f || tail_view.z >= -0.001f) {
			continue;
		}
		float* instance = buffer + visible_count * DRIFT_PLASMA_BUFFER_STRIDE;
		for (int component = 0; component < DRIFT_PLASMA_BUFFER_STRIDE; ++component) {
			instance[component] = 0.0f;
		}
		// The shader consumes camera-space tail/head endpoints from the first
		// transform column and origin, exactly like the GX ribbon callback.
		instance[0] = tail_view.x;
		instance[4] = tail_view.y;
		instance[8] = tail_view.z;
		instance[5] = 1.0f;
		instance[10] = 1.0f;
		instance[3] = head_view.x;
		instance[7] = head_view.y;
		instance[11] = head_view.z;
		instance[12] = particle.red;
		instance[13] = particle.green;
		instance[14] = particle.blue;
		instance[15] = 1.0f;
		instance[16] = particle.width * particle.render_width_scale;
		++visible_count;
	}
	if (visible_count > 0) {
		multimesh->set_buffer(runtime.render_buffer);
	}
	multimesh->set_visible_instance_count(visible_count);
}

void GameSim::reset_drift_plasma_effects(bool release_render_node)
{
	if (!drift_plasma_runtime) {
		return;
	}
	DriftPlasmaRuntime& runtime = *drift_plasma_runtime;
	drift_plasma_clear(runtime);
	runtime.last_sim_tick = -1;
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

void GameSim::destroy_drift_plasma_runtime()
{
	if (!drift_plasma_runtime) {
		return;
	}
	reset_drift_plasma_effects(true);
	delete drift_plasma_runtime;
	drift_plasma_runtime = nullptr;
}

} // namespace godot
