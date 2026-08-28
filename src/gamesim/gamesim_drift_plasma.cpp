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
static constexpr int DRIFT_PLASMA_CAPACITY = 190;
static constexpr int DRIFT_PLASMA_BUFFER_STRIDE = 20;
static constexpr int DRIFT_PLASMA_REMOTE_EMISSION_INTERVAL = 5;
static constexpr float DRIFT_PLASMA_LATERAL_THRESHOLD = 1.0f;
// GFZE01 lbl_1_rodata_2D70 + 0xE8. The adjacent +0xEC value supplies the
// separate 5.0-unit corner-distance clamp for emission sampling.
static constexpr float DRIFT_PLASMA_CORNER_MOTION_SCALE = 0.93f;
static constexpr float DRIFT_PLASMA_CORNER_DISTANCE_MAX = 5.0f;
static constexpr float DRIFT_PLASMA_RANDOM_MOTION = 0.4f;
static constexpr float DRIFT_PLASMA_NORMAL_SPEED = 0.8f;
static constexpr float DRIFT_PLASMA_GRAVITY_Y = -0.03f;
static constexpr float DRIFT_PLASMA_DAMPING_EASE = 0.05f;
static constexpr float DRIFT_PLASMA_WIDTH_EASE = 0.1f;
static constexpr float DRIFT_PLASMA_HALF_PI = 1.57079632679489661923f;

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
	uint16_t initial_life = 0;
	int16_t car_index = -1;
	uint8_t update_count = 0;
	uint8_t active = 0;
};

struct DriftPlasmaRuntime {
	std::array<DriftPlasmaParticle, DRIFT_PLASMA_CAPACITY> particles;
	PackedFloat32Array render_buffer;
	MultiMeshInstance3D* multimesh_instance = nullptr;
	uint32_t rng_state = 1u;
	int last_sim_tick = -1;
};

static uint16_t drift_plasma_rand_u15(uint32_t& state)
{
	// GFZE01 fn_1_584AC: the shared effects LCG used by ET_DRIFT_PTCL.
	state = state * 0x41c64e6du + 0x3039u;
	return static_cast<uint16_t>((state >> 16u) & 0x7fffu);
}

static float drift_plasma_rand01(uint32_t& state)
{
	return static_cast<float>(drift_plasma_rand_u15(state)) * (1.0f / 32767.0f);
}

static float drift_plasma_rand_range(uint32_t& state, float minimum, float maximum)
{
	return minimum + (maximum - minimum) * drift_plasma_rand01(state);
}

static bool drift_plasma_frame(
	const PhysicsCarSoA& soa,
	int lane,
	SimVec3& out_lateral)
{
	const SimVec3 forward(
		-soa.basis_physical_c2x[lane],
		-soa.basis_physical_c2y[lane],
		-soa.basis_physical_c2z[lane]);
	const SimVec3 surface_normal = LOAD_INDEXED_VEC3(soa, track_surface_normal, lane);
	// GX computes forward x normal, but its local X axis has the opposite
	// handedness from Godot. A reflected cross product needs this sign change.
	out_lateral = surface_normal.cross(forward);
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
	if (particle.update_count < 0xffu) {
		++particle.update_count;
	}
	particle.older_position = particle.previous_position;
	particle.previous_position = particle.position;
	particle.velocity.y += DRIFT_PLASMA_GRAVITY_Y;
	particle.velocity *= 1.0f - particle.damping;
	particle.position += particle.velocity;
	particle.damping += DRIFT_PLASMA_DAMPING_EASE * (particle.target_damping - particle.damping);
	particle.width += DRIFT_PLASMA_WIDTH_EASE * (particle.target_width - particle.width);
}

static DriftPlasmaParticle* drift_plasma_allocate(DriftPlasmaRuntime& runtime)
{
	// GFZE01 fn_1_58F50 scans its 190 records from the beginning and drops an
	// allocation when the pool is full, preserving every live effect.
	for (DriftPlasmaParticle& particle : runtime.particles) {
		if (!particle.active) {
			return &particle;
		}
	}
	return nullptr;
}

static void drift_plasma_spawn(
	DriftPlasmaRuntime& runtime,
	const SimVec3& position,
	const SimVec3& velocity,
	float intensity,
	float speed_kmh,
	bool half_render_width,
	int car_index)
{
	// fn_1_6AF70 constructs all descriptor fields before asking the pool for a
	// record. Preserve that RNG order even when the fixed pool is exhausted.
	const uint16_t life = static_cast<uint16_t>(60.0f *
		(0.05f + 0.08f * intensity * drift_plasma_rand01(runtime.rng_state)));
	const SimVec3 position_jitter(
		drift_plasma_rand_range(runtime.rng_state, -DRIFT_PLASMA_RANDOM_MOTION, DRIFT_PLASMA_RANDOM_MOTION),
		drift_plasma_rand_range(runtime.rng_state, -DRIFT_PLASMA_RANDOM_MOTION, DRIFT_PLASMA_RANDOM_MOTION),
		drift_plasma_rand_range(runtime.rng_state, -DRIFT_PLASMA_RANDOM_MOTION, DRIFT_PLASMA_RANDOM_MOTION));
	const float target_width = 0.5f * (0.5f + 0.3f * drift_plasma_rand01(runtime.rng_state));
	const float width = 0.5f * (0.4f + 0.2f * intensity * drift_plasma_rand01(runtime.rng_state));
	const float color_scale = 0.5f + 0.3f * drift_plasma_rand01(runtime.rng_state);

	DriftPlasmaParticle* allocated = drift_plasma_allocate(runtime);
	if (!allocated) {
		return;
	}
	DriftPlasmaParticle& particle = *allocated;
	particle = DriftPlasmaParticle();
	particle.active = 1;
	particle.car_index = static_cast<int16_t>(car_index);
	particle.velocity = velocity;
	particle.position = position + position_jitter - velocity;
	particle.previous_position = particle.position;
	particle.older_position = particle.position;
	particle.target_width = target_width;
	particle.width = width;
	particle.render_width_scale = half_render_width ? 0.5f : 1.0f;
	particle.damping = 0.05f * drift_plasma_rand01(runtime.rng_state);
	particle.target_damping = 0.07f + speed_kmh * (1.0f / 20000.0f);
	particle.red = 0.8f * color_scale;
	particle.green = 0.5f * color_scale;
	particle.blue = particle.green;
	particle.life = life;
	particle.initial_life = life;

	// The emitting machine is itself an earlier record in GX's shared effect
	// pool. fn_1_58694 reaches a newly allocated drift particle later in the
	// same forward scan, so its seeded one-step-behind position is advanced
	// before the first draw. Our particles live in a separate pool and must do
	// that same first update explicitly.
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
		if (!drift_plasma_frame(soa, lane, lateral)) {
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
			// Official and custom property files may use either left/right corner
			// ordering. Classify the physical side from the authored offset; the
			// lateral vector above is local -X.
			const bool positive_side_corner = soa.tilt_offset_x[point] < 0.0f;
			if ((lateral_delta > 0.0f) != positive_side_corner) {
				continue;
			}
			const float side_sign = positive_side_corner ? 1.0f : -1.0f;
			const float same_side_delta = lateral_delta * side_sign;
			const float unclamped_intensity = same_side_delta - DRIFT_PLASMA_LATERAL_THRESHOLD;
			if (unclamped_intensity < 0.0f) {
				continue;
			}
			const float intensity = std::min(unclamped_intensity, 1.0f);
			const float emit_strength = std::clamp(intensity, 0.3f, 1.0f);
			const SimVec3 corner_normal(
				soa.tilt_up_vector_2_x[point],
				soa.tilt_up_vector_2_y[point],
				soa.tilt_up_vector_2_z[point]);

			SimVec3 velocity = corner_delta * DRIFT_PLASMA_CORNER_MOTION_SCALE;
			velocity -= corner_normal * velocity.dot(corner_normal);
			velocity += SimVec3(
				drift_plasma_rand_range(runtime.rng_state, -DRIFT_PLASMA_RANDOM_MOTION, DRIFT_PLASMA_RANDOM_MOTION),
				drift_plasma_rand_range(runtime.rng_state, -DRIFT_PLASMA_RANDOM_MOTION, DRIFT_PLASMA_RANDOM_MOTION),
				drift_plasma_rand_range(runtime.rng_state, -DRIFT_PLASMA_RANDOM_MOTION, DRIFT_PLASMA_RANDOM_MOTION));
			velocity += corner_normal * (DRIFT_PLASMA_NORMAL_SPEED * emit_strength);
			velocity += lateral * (side_sign * emit_strength);

			const float corner_speed = std::clamp(
				corner_delta.length(),
				0.0f,
				DRIFT_PLASMA_CORNER_DISTANCE_MAX);
			const int particle_count = corner_speed <= 2.0f ? 1 : (corner_speed <= 4.0f ? 2 : 3);
			for (int particle_index = 0; particle_index < particle_count; ++particle_index) {
				// GFZE01 fn_1_6AF70 ultimately starts every burst particle at the
				// current corner and then applies its random offset.
				drift_plasma_spawn(
					runtime,
					position,
					velocity,
					intensity,
					soa.speed_kmh[lane],
					corner < 2,
					car_index);
			}
		}
	}
}

struct DriftPlasmaBasis {
	Vector3 c0 = Vector3(1.0f, 0.0f, 0.0f);
	Vector3 c1 = Vector3(0.0f, 1.0f, 0.0f);
	Vector3 c2 = Vector3(0.0f, 0.0f, 1.0f);
};

static void drift_plasma_post_rotate_x(DriftPlasmaBasis& basis, float angle)
{
	const float sine = std::sin(angle);
	const float cosine = std::cos(angle);
	const Vector3 old_c1 = basis.c1;
	const Vector3 old_c2 = basis.c2;
	basis.c1 = old_c1 * cosine + old_c2 * sine;
	basis.c2 = old_c1 * -sine + old_c2 * cosine;
}

static void drift_plasma_post_rotate_y(DriftPlasmaBasis& basis, float angle)
{
	const float sine = std::sin(angle);
	const float cosine = std::cos(angle);
	const Vector3 old_c0 = basis.c0;
	const Vector3 old_c2 = basis.c2;
	basis.c0 = old_c0 * cosine - old_c2 * sine;
	basis.c2 = old_c0 * sine + old_c2 * cosine;
}

static void drift_plasma_write_vertex(float* instance, int channel, const Vector3& vertex)
{
	instance[channel] = vertex.x;
	instance[4 + channel] = vertex.y;
	instance[8 + channel] = vertex.z;
}

static void drift_plasma_write_instance(
	float* instance,
	const DriftPlasmaBasis& basis,
	const Vector3& origin,
	float quad_half_extent,
	const DriftPlasmaParticle& particle)
{
	std::fill_n(instance, DRIFT_PLASMA_BUFFER_STRIDE, 0.0f);
	const Vector3 horizontal = basis.c0 * quad_half_extent;
	const Vector3 vertical = basis.c1 * quad_half_extent;
	// MultiMesh transform columns are data channels here. Supplying the final
	// camera-space vertices directly prevents Godot from treating GX's already
	// camera-relative ribbon matrix as another world transform.
	drift_plasma_write_vertex(instance, 0, origin - horizontal - vertical);
	drift_plasma_write_vertex(instance, 1, origin + horizontal - vertical);
	drift_plasma_write_vertex(instance, 2, origin + horizontal + vertical);
	drift_plasma_write_vertex(instance, 3, origin - horizontal + vertical);
	const float life_fraction = static_cast<float>(particle.life) /
		static_cast<float>(particle.initial_life);
	const float life_scale = std::sin(life_fraction * DRIFT_PLASMA_HALF_PI);
	instance[12] = particle.red * life_scale;
	instance[13] = particle.green * life_scale;
	instance[14] = particle.blue * life_scale;
	instance[15] = 1.0f;
	instance[16] = 0.0f;
	instance[17] = 0.0f;
	instance[18] = 0.0f;
	instance[19] = 0.0f;
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
	const Transform3D current_view_from_world = camera->get_global_transform().affine_inverse();
	Transform3D previous_view_from_world = current_view_from_world;
	if (camera == gameplay_camera_node && gameplay_camera.is_valid()) {
		previous_view_from_world = gameplay_camera->get_previous_render_transform(alpha).affine_inverse();
	}
	int visible_count = 0;
	for (const DriftPlasmaParticle& particle : runtime.particles) {
		// GX exposes a newly allocated record after its first complete 60 Hz
		// update. HFR follows the same boundary so both interpolated endpoints
		// represent complete adjacent particle states.
		if (!particle.active || particle.update_count < 2u) {
			continue;
		}
		const SimVec3 previous_position = particle.older_position.lerp(particle.previous_position, alpha);
		const SimVec3 current_position = particle.previous_position.lerp(particle.position, alpha);
		// GX transforms history with the previous camera and the current point
		// with the current camera. This removes camera-follow motion shared by the
		// machine and particle, preserving particle-relative streak length.
		const Vector3 previous_view = previous_view_from_world.xform(gd_vec3(previous_position));
		Vector3 head_view = current_view_from_world.xform(gd_vec3(current_position));
		Vector3 tail_view = previous_view * 1.5f - head_view * 0.5f;
		if (head_view.z >= 0.0f || tail_view.z >= 0.0f) {
			continue;
		}
		float* instance = buffer + visible_count * DRIFT_PLASMA_BUFFER_STRIDE;
		DriftPlasmaBasis basis;
		Vector3 origin;
		const Vector2 planar_delta(head_view.x - tail_view.x, head_view.y - tail_view.y);
		if (planar_delta.length_squared() == 0.0f) {
			// Keep the degenerate case finite. GX uses the machine basis here;
			// MaxX Throttle uses its interpolated visual basis so the rare fallback
			// cannot visibly jump between simulation ticks.
			if (particle.car_index < 0 ||
					particle.car_index >= static_cast<int>(render_final_current_transforms.size()) ||
					particle.car_index >= static_cast<int>(render_final_prev_transforms.size())) {
				continue;
			}
			const SimTransform visual_transform = interpolate_sim_transform(
				render_final_prev_transforms[particle.car_index],
				render_final_current_transforms[particle.car_index],
				alpha);
			const Basis visual_view_basis = current_view_from_world.basis * gd_transform(visual_transform).basis;
			basis.c0 = visual_view_basis.get_column(0);
			basis.c1 = visual_view_basis.get_column(1);
			basis.c2 = visual_view_basis.get_column(2);
			origin = head_view;
			drift_plasma_post_rotate_x(basis, DRIFT_PLASMA_HALF_PI);
		} else {
			const Vector2 planar_direction = planar_delta.normalized();
			const Vector2 expansion = planar_direction * (particle.width * 0.1f);
			tail_view.x -= expansion.x;
			tail_view.y -= expansion.y;
			head_view.x += expansion.x;
			head_view.y += expansion.y;

			const Vector3 segment = head_view - tail_view;
			const float segment_length = segment.length();
			origin = (head_view + tail_view) * 0.5f;
			const float pitch = std::atan2(-segment.y, std::sqrt(segment.x * segment.x + segment.z * segment.z));
			const float yaw = std::atan2(segment.x, segment.z);
			drift_plasma_post_rotate_y(basis, yaw);
			drift_plasma_post_rotate_x(basis, pitch);
			// lbl_8006E15C applies (1, 1, length / width). The queued quad's
			// separate 8*width scalar supplies the two plane dimensions.
			basis.c2 *= segment_length / particle.width;
			drift_plasma_post_rotate_x(basis, DRIFT_PLASMA_HALF_PI);

			// GX calculates -transpose(M.linear) * M.translation after the
			// nonuniform length scale, then uses that vector for the final twist.
			// Using the raw origin here was the major render mismatch in the old
			// port: it applied the wrong rotation to the already stretched basis.
			const Vector3 camera_relative(
				-basis.c0.dot(origin),
				-basis.c1.dot(origin),
				-basis.c2.dot(origin));
			drift_plasma_post_rotate_y(
				basis,
				std::atan2(camera_relative.x, camera_relative.z));
		}

		const float quad_half_extent = 8.0f * particle.width * particle.render_width_scale;
		drift_plasma_write_instance(instance, basis, origin, quad_half_extent, particle);
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
