#include "physics_car.h"
#include "godot_cpp/variant/utility_functions.hpp"
#include "godot_cpp/classes/engine.hpp"
#include "godot_cpp/classes/object.hpp"
#include <cmath>

int PhysicsCar::find_current_checkpoint_recursive(const godot::Vector3 &use_pos, int cp_index, int iterations)
{
	if (iterations > 10)
	{
		return -1;
	}
	if (!current_track->checkpoints[cp_index].end_plane.is_point_over(use_pos) && current_track->checkpoints[cp_index].start_plane.is_point_over(use_pos))
	{
		current_collision_checkpoint = cp_index;
		return cp_index;
	}
	else
	{
		for (int i = 0; i < current_track->checkpoints[cp_index].num_neighboring_checkpoints; i++)
		{
			int neighbor_index = current_track->checkpoints[cp_index].neighboring_checkpoints[i];
			int found_checkpoint = find_current_checkpoint_recursive(use_pos, neighbor_index, iterations + 1);
			if (found_checkpoint)
			{
				return found_checkpoint;
			}
		}
	}
	return -1;
}

void PhysicsCar::get_closest_road_data_at_point(RoadData &out_data, godot::Vector3 &in_point, bool strict, uint16_t start_cp)
{
	std::vector<int> checkpoints_to_test;
	if (!strict)
	{
		checkpoints_to_test = current_track->get_viable_checkpoints(in_point);
	}
	else {
		current_collision_checkpoint = find_current_checkpoint_recursive(in_point, current_collision_checkpoint, 0);
		if (current_collision_checkpoint != -1)
		{
			checkpoints_to_test.push_back(current_collision_checkpoint);
		}
	}

	if (checkpoints_to_test.size() == 0)
	{
		out_data.terrain = 0;
		out_data.cp_idx = -1;
		return;
	}

	// Phase 1: Find the best checkpoint using a cheap cost metric
	float lowest_cost = 1000000.0f;
	int best_checkpoint_idx = -1;
	godot::Vector3 best_checkpoint_t; // The best (tx, ty, tz)
	godot::Vector2 best_road_t;       // The best road_t associated with the winner

	for (int i = 0; i < checkpoints_to_test.size(); ++i)
	{
		const int checkpoint_idx = checkpoints_to_test[i];
		CollisionCheckpoint* current_point = &current_track->checkpoints[checkpoint_idx];

		godot::Vector3 p1 = current_point->start_plane.project(in_point);
		godot::Vector3 p2 = current_point->end_plane.project(in_point);
		float cp_t = get_closest_t_on_segment(in_point, p1, p2);

		godot::Basis basis_at_t;
		basis_at_t[0] = current_point->orientation_start[0].lerp(current_point->orientation_end[0], cp_t).normalized();
		basis_at_t[2] = current_point->orientation_start[2].lerp(current_point->orientation_end[2], cp_t).normalized();
		basis_at_t[1] = current_point->orientation_start[1].lerp(current_point->orientation_end[1], cp_t).normalized();

		godot::Vector3 midpoint = current_point->position_start.lerp(current_point->position_end, cp_t);
		godot::Plane separating_x_plane = godot::Plane(basis_at_t[0], midpoint);
		godot::Plane separating_y_plane = godot::Plane(basis_at_t.get_column(1), midpoint);

		float x_radius_at_t = lerp(current_point->x_radius_start_inv, current_point->x_radius_end_inv, cp_t);
		float y_radius_at_t = lerp(current_point->y_radius_start_inv, current_point->y_radius_end_inv, cp_t);

		float tx = separating_x_plane.distance_to(in_point) * x_radius_at_t;
		float ty = separating_y_plane.distance_to(in_point) * y_radius_at_t;

		float cost = tx * tx + ty * ty;

		if (cost < lowest_cost)
		{
			lowest_cost = cost;
			best_checkpoint_idx = checkpoint_idx;

			float tz = remap_float(cp_t, 0.0f, 1.0f, current_point->t_start, current_point->t_end);
			best_checkpoint_t = godot::Vector3(tx, ty, tz);
			current_track->segments[current_point->road_segment].road_shape->find_t_from_relative_pos(best_road_t, best_checkpoint_t);
		}
	}

	if (best_checkpoint_idx == -1) {
		return;
	}
	out_data.terrain = 0; // todo: check embeds for terrain if the mask wants us to
	out_data.cp_idx = best_checkpoint_idx;
	out_data.spatial_t = best_checkpoint_t;
	out_data.road_t = best_road_t;

	const int closest_segment_idx = current_track->checkpoints[best_checkpoint_idx].road_segment;
	current_track_segment = closest_segment_idx;
	current_track->segments[closest_segment_idx].road_shape->get_oriented_transform_at_time(out_data.closest_surface, current_road_t);
	current_track->segments[closest_segment_idx].curve_matrix->sample(out_data.closest_root, current_road_t.y);
}

void PhysicsCar::initialize()
{
	PlayerInput current_input;
	position = godot::Vector3(0.0f, 0.0f, 0.0f);
	prev_position = godot::Vector3(0.0f, 0.0f, 0.0f);
	desired_position = godot::Vector3(0.0f, 0.0f, 0.0f);
	apparent_velocity = godot::Vector3(0.0f, 0.0f, 0.0f);
	orientation = BASIS_IDENTITY;
	prev_orientation = BASIS_IDENTITY;
	car_transform = T3D_IDENTITY;
	travel_direction = godot::Vector3(0.0f, 0.0f, 0.0f);
	angle_velocity = godot::Vector3(0.0f, 0.0f, 0.0f);
	gravity_orientation = godot::Vector3(0.0f, 1.0f, 0.0f);
	closest_roadt3d = T3D_IDENTITY;
	current_checkpoint_t = godot::Vector3(0.0f, 0.0f, 0.0f);
	current_steer_state = NORMAL;
	base_speed = 0.0f;
	apparent_speed = 0.0f;
	apparent_speed_kmh = 0.0f;
	health = 0.0f;
	drift_dot = 0.0f;
	boost_time = 0.0f;
	dashplate_time = 0.0f;
	turbo = 0.0f;
	air_tilt = 0.0f;
	ground_air_time = 0.0f;
	turn_reaction_effect = 0.0f;
	current_steering_input = 0.0f;
	previous_steering_input = 0.0f;
	current_steering = 0.0f;
	previous_steering = 0.0f;
	current_strafe = 0.0f;
	num_collision_points = 0;
	num_suspension_points = 0;
	grounded = true;
}


void PhysicsCar::preprocess_car()
{
	current_input = PlayerInput::from_player_input();
	apparent_velocity = (position - prev_position) / _TICK_DELTA;
	apparent_speed = apparent_velocity.length();
	apparent_speed_kmh = apparent_speed * _U_TO_KMH;
	car_transform = godot::Transform3D(orientation, position);

	if (current_input.brake)
	{
		base_speed = 0.0f;
		position = godot::Vector3(0.0f, 0.0f, 0.0f);
		grounded = false;
		for (int i = 0; i < num_suspension_points; i++)
		{
			suspension_points[i].force_at_point = 0.0f;
		}
	}

	if (grounded && current_road_embed == 0)
	{
		health = fminf(car_properties->max_health, health + car_properties->health_recharge_rate * _TICK_DELTA);
	}

	prev_position = position;
	prev_orientation = orientation;
	previous_steering = current_steering;
	previous_steering_input = current_input.steer_horizontal;
}

float PhysicsCar::calculate_top_speed()
{
	float base_max_speed = 900.0f + (1.0 + (car_properties->weight * 0.01)) + car_properties->max_speed * 100 + turbo * 50;
	if (boost_time > 0.0f && dashplate_time == 0.0f)
	{
		base_max_speed *= car_properties->boost_topspeed_mult;
	}
	else if (dashplate_time > 0.0f && boost_time == 0.0f)
	{
		base_max_speed *= car_properties->dash_topspeed_mult;
	}
	else if (boost_time > 0.0f && dashplate_time > 0.0f)
	{
		base_max_speed *= car_properties->boost_and_dash_topspeed_mult;
	}
	return _KMH_TO_U * base_max_speed;
}

float PhysicsCar::calculate_acceleration(float in_speed)
{
	float base_accel = lerp(car_properties->acceleration * 10.0f, 0.0f, remap_float(in_speed, 0.0f, calculate_top_speed(), 0.0f, 1.0f)) * _TICK_DELTA;
	if (boost_time > 0.0f && dashplate_time == 0.0f)
	{
		base_accel *= car_properties->boost_accel_mult;
	}
	else if (dashplate_time > 0.0f && boost_time == 0.0f)
	{
		base_accel *= car_properties->dash_accel_mult;
	}
	else if (boost_time > 0.0f && dashplate_time > 0.0f)
	{
		base_accel *= car_properties->boost_and_dash_accel_mult;
	}
	return base_accel;
}

void PhysicsCar::process_car_steering()
{
	//godot::Object* dd2d = godot::Engine::get_singleton()->get_singleton("DebugDraw2D");
	//godot::Object* dd3d = godot::Engine::get_singleton()->get_singleton("DebugDraw3D");
	float steer_difference = current_input.steer_horizontal - previous_steering_input;
	float steer_input_strength = fabsf(current_input.steer_horizontal);
	turn_reaction_effect += steer_difference * car_properties->steer_reaction;
	turn_reaction_effect += -turn_reaction_effect * car_properties->steer_reaction_damp * _TICK_DELTA;
	current_steering = lerp(current_steering, current_input.steer_horizontal, car_properties->steer_acceleration * _TICK_DELTA);

	godot::Transform3D use_transform = car_transform;
	use_transform.basis.transpose();

	float final_steer_target = (current_steer_state == NORMAL) ? car_properties->steer_speed_target : car_properties->steer_speed_target_drift;

	float total_steering = (current_steering + turn_reaction_effect) * final_steer_target * 0.0174533f;
	orientation = orientation.rotated_local(gravity_orientation, total_steering * _TICK_DELTA);

	godot::Vector3 forward_axis_flat = orientation[2].slide(closest_roadt3d.basis[1]).normalized();
	godot::Vector3 cross_to = forward_axis_flat.cross(travel_direction);
	float unsigned_angle_from_travel_dir = forward_axis_flat.angle_to(travel_direction);
	float steer_sign = cross_to.dot(gravity_orientation);
	float angle_from_travel_dir = (steer_sign < 0.0f) ? -unsigned_angle_from_travel_dir : unsigned_angle_from_travel_dir;
	float angle_sample = unsigned_angle_from_travel_dir * ONE_DIV_BY_PI * 2.0;

	current_strafe = move_float_toward(current_strafe, current_input.strafe_right - current_input.strafe_left, car_properties->strafe_accel * _TICK_DELTA);

	float strafe_steer_parity = sgn<float>(angle_from_travel_dir) * current_strafe;

	godot::Vector3 strafe_add = orientation[0] * current_strafe * car_properties->strafe_power * apparent_speed * -0.01f;

	//dd2d->call("set_text", "strafe part 1", orientation[0]);
	//dd2d->call("set_text", "strafe part 2", current_strafe);
	//dd2d->call("set_text", "strafe part 3", car_properties->strafe_power);
	//dd2d->call("set_text", "strafe part 4", apparent_speed);

	//dd2d->call("set_text", "strafe add 1", strafe_add);

	if (current_steer_state == NORMAL || !grounded)
	{
		strafe_add += orientation[0] * (total_steering * apparent_speed * car_properties->turn_strafe_effect * -0.01f);
	}
	if (current_steer_state != NORMAL)
	{
		if (strafe_steer_parity > 0.0f)
		{
			strafe_add *= lerp(1.0f, car_properties->strafe_qt_mult, fabsf(current_strafe * fabsf(current_input.steer_horizontal)));
			strafe_add = strafe_add.lerp(travel_direction * travel_direction.dot(strafe_add), car_properties->strafe_qt_laterality * fabsf(current_input.steer_horizontal));
			//dd2d->call("set_text", "strafe add 3", strafe_add);
		}
		else
		{
			strafe_add *= lerp(1.0f, car_properties->strafe_mts_mult, angle_sample);
			strafe_add = strafe_add.lerp(travel_direction * travel_direction.dot(strafe_add), car_properties->strafe_mts_laterality);
			//dd2d->call("set_text", "strafe add 4", strafe_add);
		}
	}
	

	if (grounded)
	{
		drift_dot = apparent_speed * fabsf(total_steering) + fabsf(angle_from_travel_dir) * 5000.0f;
		//dd2d->call("set_text", "drift dot", drift_dot);
		//dd2d->call("set_text", "drift threshold", 1000.0f * car_properties->grip);
		if (drift_dot > 100.0f * car_properties->grip || (current_input.strafe_left > 0.05 && current_input.strafe_right > 0.05))
		{
			current_steer_state = DRIFT;
		}
		else if (sgn<float>(current_strafe) != sgn<float>(total_steering) || fabsf(total_steering) < 1.0)
		{
			current_steer_state = NORMAL;
		};

		if (current_steer_state == DRIFT)
		{
			float drift_add = drift_dot * car_properties->drift_accel * _TICK_DELTA * 0.00001;
			if (drift_add > 0.0f)
			{
				drift_add *= current_input.accelerate;
			}
			base_speed += drift_dot * car_properties->drift_accel * _TICK_DELTA * 0.00001;
		};
	}

	if (grounded)
	{
		float use_angle_mod = 1.0f;
		if (current_steer_state == DRIFT)
		{
			use_angle_mod = car_properties->vel_redir_drift->sample(angle_sample);
		}
		else
		{
			use_angle_mod = car_properties->vel_redir->sample(angle_sample);
		}
		if (strafe_steer_parity > 0.0f)
		{
			use_angle_mod = lerp(use_angle_mod, car_properties->vel_redir_quickturn->sample(angle_sample), fabsf(current_strafe));
		}
		else if (strafe_steer_parity < 0.0f)
		{
			use_angle_mod = lerp(use_angle_mod, car_properties->vel_redir_mts->sample(angle_sample), fabsf(current_strafe));
		}
		//dd2d->call("set_text", "angle_mod", use_angle_mod);
		float use_speed_mod = car_properties->vel_redir_mult_by_speed->sample(remap_float(apparent_speed_kmh, 0.0f, 3000.0f, 0.0f, 1.0f));
		//dd2d->call("set_text", "speed_mod", use_speed_mod);
		float slerp_t = fminf(1.0f, ((5.0f / fmaxf(0.01f, unsigned_angle_from_travel_dir)) * use_angle_mod * use_speed_mod));
		//dd2d->call("set_text", "angle", unsigned_angle_from_travel_dir);
		//dd2d->call("set_text", "slerp_t", slerp_t);
		travel_direction = travel_direction.slerp(forward_axis_flat, slerp_t);
		air_velocity = godot::Vector3(0.0f, 0.0f, 0.0f);
		air_tilt = 0.0f;
	}
	else {
		travel_direction = travel_direction.slerp(orientation[2], 8.0f * _TICK_DELTA);
		//air_velocity += godot::Vector3(0.0f, _GRAVITY, 0.0f) * _TICK_DELTA;
		float old_air_tilt = air_tilt;
		air_tilt = fminf(fmaxf(air_tilt + current_input.steer_vertical * _TICK_DELTA * 4, -1.0f), 1.0f);

		godot::Vector3 up_dir = godot::Vector3(0.f, 1.f, 0.f);

		if (current_road_t[0] > -1.0f || current_road_t[0] < 1.0f)
		{
			up_dir = closest_roadt3d.basis[1];
		}

		godot::Quaternion to_gravity = godot::Quaternion(gravity_orientation, up_dir);
		if (to_gravity.get_angle() > 0.0001) {
			godot::Quaternion gravity_change = godot::Quaternion(to_gravity.get_axis(), to_gravity.get_angle() * _TICK_DELTA * -8.f);
			gravity_orientation.rotate(to_gravity.get_axis(), to_gravity.get_angle() * _TICK_DELTA * 8.f);
			orientation.rotate(gravity_change);
		}

		orientation = orientation.rotated_local(orientation[0], air_tilt - old_air_tilt);
		////dd3d->call("draw_arrow", position, position + orientation[0] * 4, godot::Color(1.0f, 0.0f, 0.0f), 0.25, true, _TICK_DELTA);
	}

	angle_velocity += -angle_velocity * car_properties->angle_drag * _TICK_DELTA;

	godot::Vector3 travel_dir_flat = (travel_direction - closest_roadt3d.basis[1] * closest_roadt3d.basis[1].dot(travel_direction)).normalized();

	godot::Vector3 total_movement = (travel_dir_flat * base_speed + strafe_add) * _TICK_DELTA;

	if (grounded)
	{
		float total_force = 0.0f;
		for (int i = 0; i < num_suspension_points; i++)
		{
			// var global_point : Vector3 = current_transform * point
			// var about_origin : = global_point - current_position
			// var depth : = (global_point - current_position).dot(gravity_basis.y)
			// if gravity_basis.y.dot(current_transform.basis.y) < 0:
				// depth *= -1.0
				// var reorient_strength : = 16.0
			// if depth > 0:
				// reorient_strength = 8.0
				// angle_vel += about_origin.cross(gravity_basis.y) * depth * -reorient_strength
			
			godot::Vector3 global_point_origin = use_transform.xform(suspension_points[i].origin_point);
			godot::Vector3 about_car = global_point_origin - position;
			float depth = (global_point_origin - position).dot(gravity_orientation);
			if (gravity_orientation.dot(orientation[1]) < 0.0f)
			{
				depth *= -1.0;
			}
			if (depth > 0)
			{
				depth *= 0.5;
			}
			angle_velocity += about_car.cross(gravity_orientation) * depth * suspension_points[i].spring_strength * _TICK_DELTA * 10.0f;
			// godot::Vector3 global_point_origin = use_transform.xform(suspension_points[i].origin_point);
			// godot::Vector3 global_point_target = use_transform.xform(suspension_points[i].origin_point + suspension_points[i].target_dir * suspension_points[i].max_length);
			// godot::Vector3 about_surface = global_point_target - closest_roadt3d.origin;
			// godot::Vector3 about_car = global_point_target - position;
			// float point_depth = about_surface.dot(closest_roadt3d.basis[1]);
			// if (point_depth > 0.0f)
			// {
			// 	point_depth *= 0.5f;
			// }
			godot::Vector3 movement_plus_gravity = (apparent_velocity - closest_roadt3d.basis[1] * 4.0f) * _TICK_DELTA;
			float surface_force = movement_plus_gravity.dot(closest_roadt3d.basis[1]) * car_properties->weight;
			suspension_points[i].force_at_point += -surface_force * _TICK_DELTA;
			suspension_points[i].force_at_point += (-suspension_points[i].force_at_point + suspension_points[i].target_length) * suspension_points[i].spring_strength * _TICK_DELTA;
			total_force += suspension_points[i].force_at_point;
			// godot::Vector3 angle_vel_add = about_car.cross(-closest_roadt3d.basis[1]) * point_depth * suspension_points[i].spring_strength;
			// if (orientation[1].dot(closest_roadt3d.basis[1]) < 0.0f)
			// {
			// 	angle_vel_add *= -1.f;
			// }
			// angle_velocity += angle_vel_add * _TICK_DELTA;
			//dd3d->call("draw_sphere", global_point_origin, 0.25f, godot::Color(1.f, 1.f, 0.f), _TICK_DELTA);
			//dd3d->call("draw_sphere", global_point_target, 0.25f, godot::Color(0.f, 1.f, 1.f), _TICK_DELTA);
			//dd3d->call("draw_arrow", global_point_origin, global_point_origin + closest_roadt3d.basis[1] * suspension_points[i].force_at_point * 0.1f, godot::Color(0.0f, 0.0f, 1.0f), 0.25, true, _TICK_DELTA);
			//dd2d->call("set_text", "force at point", suspension_points[i].force_at_point);
		}
		if (total_force < 0.0f)
		{
			grounded = false;
			air_velocity = orientation[2] * base_speed;
			for (int i = 0; i < num_suspension_points; i++)
			{
				suspension_points[i].force_at_point = 0.0f;
			}
		}
	}
	else
	{
		godot::Vector3 old_vel = air_velocity;
		godot::Vector3 flat_vel = air_velocity.slide(gravity_orientation);
		godot::Vector3 up_vel = old_vel - flat_vel;
		godot::Vector3 flat_dir = orientation[2].slide(gravity_orientation).normalized();
		flat_vel = flat_dir * flat_vel.length();

		air_velocity = flat_vel + up_vel;
		air_velocity += gravity_orientation * -120.0f * _TICK_DELTA;

		godot::Vector3 air_redirect = gravity_orientation * fmaxf(0.0f, apparent_velocity.dot(gravity_orientation)) * _TICK_DELTA * 2.5f;
		air_redirect += orientation[1] * apparent_velocity.dot(orientation[1]) * _TICK_DELTA * 1.5f;

		air_velocity += -air_redirect;
		air_velocity = air_velocity.normalized() * old_vel.length();
		air_velocity += gravity_orientation * fminf(0.0f, -120.f + apparent_speed) * _TICK_DELTA;
		total_movement = (air_velocity + strafe_add) * _TICK_DELTA;
	}

	total_movement += knockback_velocity * _TICK_DELTA;
	knockback_velocity += -knockback_velocity * 6.0f * _TICK_DELTA;

	//dd3d->call("draw_arrow", position, position + strafe_add * 0.25f, godot::Color(1.0f, 1.0f, 0.0f), 0.25, true, _TICK_DELTA);
	//dd2d->call("set_text", "speed", apparent_speed_kmh);
	//dd2d->call("set_text", "current strafe", current_strafe);
	//dd2d->call("set_text", "final strafe add", strafe_add);

	desired_position = position + total_movement;

}

void PhysicsCar::process_car_acceleration()
{

	if (boost_time > 0.0f)
	{
		boost_time = fmaxf(0.0f, boost_time - _TICK_DELTA);
		health = fmaxf(0.01f, health - (car_properties->boost_energy * _TICK_DELTA) / car_properties->boost_duration);
		if (health < 0.1f)
		{
			boost_time = 0.0f;
		}
	}

	if (turbo > 0.0f)
	{
		if (boost_time == 0.0f && dashplate_time == 0.0f)
		{
			turbo = fmaxf(0.0f, turbo - car_properties->turbo_depletion * _TICK_DELTA);
		}
		else if (boost_time > 0.0f && dashplate_time == 0.0f)
		{
			turbo = fmaxf(0.0f, turbo - car_properties->turbo_depletion_boost * _TICK_DELTA);
		}
		else if (dashplate_time > 0.0f && boost_time == 0.0f)
		{
			turbo = fmaxf(0.0f, turbo - car_properties->turbo_depletion_boost * _TICK_DELTA);
		}
		else if (boost_time > 0.0f && dashplate_time > 0.0f)
		{
			turbo = fmaxf(0.0f, turbo - car_properties->turbo_depletion_boost_dash * _TICK_DELTA);
		}
		turbo = -turbo * car_properties->turbo_depletion_percentage * _TICK_DELTA;
	}

	dashplate_time = fmaxf(0.0, dashplate_time - _TICK_DELTA);

	if (current_input.boost && boost_time == 0.0f && dashplate_time == 0.0f && health > 0.1f)
	{
		boost_time = car_properties->boost_duration;
		turbo += car_properties->turbo_add_boost;
	}

	if (grounded)
	{
		float drag_factor = car_properties->drag;

		if (current_road_embed == 1)
		{
			drag_factor *= 4.0f;
		}
		base_speed += -base_speed * _TICK_DELTA * drag_factor * 0.1f;
		base_speed = fmaxf(0.0f, base_speed - drag_factor * _TICK_DELTA * 10.0f);
		base_speed += calculate_acceleration(apparent_velocity.length()) * current_input.accelerate;
	}
	else
	{
		air_velocity += -air_velocity * _TICK_DELTA * car_properties->drag * 0.1f;
		air_velocity += orientation[2] * calculate_acceleration(0.0f) * current_input.accelerate;
	}
}

void PhysicsCar::process_car_road_collision()
{
	//godot::Object* dd3d = godot::Engine::get_singleton()->get_singleton("DebugDraw3D");
	position = desired_position;
	orientation = orientation.rotated_local(angle_velocity.normalized(), angle_velocity.length() * _TICK_DELTA);
	RoadData new_road_data;
	get_closest_road_data_at_point(new_road_data, position, grounded, current_collision_checkpoint);

	if (new_road_data.cp_idx == -1){
		has_road_data = false;
		current_collision_checkpoint = -1;
		current_track_segment = current_track->checkpoints[0].road_segment;
	}else{
		has_road_data = true;
		closest_roadt3d = new_road_data.closest_surface;
		closest_roadroot = new_road_data.closest_root;
		current_collision_checkpoint = new_road_data.cp_idx;
		current_checkpoint_t = new_road_data.spatial_t;
		current_road_t = new_road_data.road_t;
		current_track_segment = current_track->checkpoints[current_collision_checkpoint].road_segment;

	}

	if (has_road_data)
	{
		prev_closest_roadt3d = closest_roadt3d;
		godot::Vector2 old_road_t = current_road_t;

		// todo: keep track of the road segment index for the current closest surface
		// so we can check data regarding that segment, like skipping rail checks for pipe/cylinder
		// also store the converted tx/ty we get from non-standard road shapes
		// so we can accurately sample modulations from pipe/cylinder and rails from open pipe/cylinder
                godot::Basis use_basis = closest_roadroot.basis.transposed();
                godot::Vector3 left_rail_pos = closest_roadroot.origin + use_basis[0];
                godot::Vector3 right_rail_pos = closest_roadroot.origin - use_basis[0];
                godot::Vector3 left_rail_plane = -use_basis[0].normalized();
                godot::Vector3 right_rail_plane = use_basis[0].normalized();
                godot::Vector3 left_rail_normal = -closest_roadt3d.basis[1].cross(closest_roadt3d.basis[2]);
                godot::Vector3 right_rail_normal = closest_roadt3d.basis[1].cross(closest_roadt3d.basis[2]);
                godot::Vector3 closest_rail_position = current_road_t.x >= 0.0f ? left_rail_pos : right_rail_pos;
                godot::Vector3 closest_rail_normal = current_road_t.x >= 0.0f ? left_rail_normal : right_rail_normal;
                godot::Vector3 closest_rail_plane = current_road_t.x >= 0.0f ? left_rail_plane : right_rail_plane;
                TrackSegment* rail_segment = &current_track->segments[current_track_segment];
                if (grounded)
                {
                        for (int i = 0; i < num_collision_points; i++)
                        {
                                godot::Transform3D use_transform = car_transform;
                                use_transform.origin = position;
                                use_transform.basis.transpose();
                                godot::Vector3 collision_point_origin = use_transform.xform(collision_points[i].origin_point);
                                float rail_height = current_road_t.x >= 0.0f ? rail_segment->left_rail_height : rail_segment->right_rail_height;
                                float vertical_dist = (collision_point_origin - closest_roadt3d.origin).dot(closest_roadt3d.basis[1]);
                                if (vertical_dist <= rail_height)
                                {
                                        float signed_dist = (collision_point_origin - closest_rail_position).dot(closest_rail_plane);
                                        if (signed_dist < 0.0f)
                                        {
                                                position += closest_rail_normal * fmaxf(-signed_dist + 0.25f, 0.0f);
                                                float old_speed = base_speed;
                                                float collision_dot = apparent_velocity.normalized().dot(closest_rail_normal);
                                                base_speed *= 1.0f + fminf(0.0f, collision_dot);
                                                travel_direction = travel_direction.slide(closest_rail_normal);
                                                knockback_velocity += closest_rail_normal * -collision_dot * (old_speed - base_speed);
                                                angle_velocity += -orientation[1].cross(closest_rail_normal) * (old_speed - base_speed) * 0.05f;
                                        }
                                }
                        }
                }
	}
	else
	{
		if (grounded)
		{
			grounded = false;
			air_velocity = orientation[2] * base_speed;
			for (int i = 0; i < num_suspension_points; i++)
			{
				suspension_points[i].force_at_point = 0.0f;
			}
		}
	}
	if (has_road_data && current_road_t[1] < 1.0f && current_road_t[1] > 0.0f)
	{
		// todo: account for x > -1.0 and x < 1.0 depending on left or right side not having rails
		if (grounded)
		{
			gravity_orientation = closest_roadt3d.basis[1];
			float lowest_dist = 100.0f;
			for (int i = 0; i < num_suspension_points; i++)
			{
				godot::Transform3D use_transform = car_transform;
				use_transform.origin = position;
				use_transform.basis.transpose();
				godot::Vector3 global_point_origin = use_transform.xform(suspension_points[i].origin_point);
				float signed_dist = (global_point_origin - closest_roadt3d.origin).dot(closest_roadt3d.basis[1]);
				if (signed_dist < lowest_dist)
				{
					position += closest_roadt3d.basis[1] * -signed_dist;
					lowest_dist = signed_dist;
				}
			}
			//for (int i = 0; i < num_suspension_points; i++)
			//{
			//	godot::Transform3D use_transform = car_transform;
			//	use_transform.origin = position;
			//	use_transform.basis.transpose();
			//	godot::Vector3 global_point_origin = use_transform.xform(suspension_points[i].origin_point);
			//}
			current_road_embed = -1;
			TrackSegment* our_current_road = &current_track->segments[current_track_segment];
			for (int i = 0; i < our_current_road->road_shape->num_embeds; i++)
			{
				RoadEmbed* cur_embed = &our_current_road->road_shape->road_embeds[i];
				if (current_road_t[1] > cur_embed->start_offset && current_road_t[1] < cur_embed->end_offset)
				{
					float embed_sample_t = (current_road_t[1] - cur_embed->start_offset) / cur_embed->end_offset;
					float left_bound_sample = cur_embed->left_border->sample(embed_sample_t);
					float right_bound_sample = cur_embed->right_border->sample(embed_sample_t);
					if (current_road_t[0] < left_bound_sample && current_road_t[0] > right_bound_sample)
					{
						current_road_embed = cur_embed->embed_type;
					}
				}
			}
		}
		else
		{
			float signed_dist_1 = (position - closest_roadt3d.origin).dot(closest_roadt3d.basis[1]);
			float signed_dist_2 = ((prev_position + closest_roadt3d.basis[1]) - prev_closest_roadt3d.origin).dot(prev_closest_roadt3d.basis[1]);
			if (signed_dist_1 <= 0.0f && signed_dist_2 >= 0.0f)
			{
				position += closest_roadt3d.basis[1] * -signed_dist_1;
				grounded = true;
				float evenness = (closest_roadt3d.basis[1].dot(orientation[1]) + 1.0f) * 0.5f;
				base_speed = air_velocity.length() * evenness;
				for (int i = 0; i < num_suspension_points; i++)
				{
					suspension_points[i].force_at_point -= apparent_velocity.dot(closest_roadt3d.basis[1]);
				}
			}

		}
	}
	else
	{
		if (grounded)
		{
			grounded = false;
			air_velocity = orientation[2] * base_speed;
			for (int i = 0; i < num_suspension_points; i++)
			{
				suspension_points[i].force_at_point = 0.0f;
			}
		}
	}
	//dd3d->call("draw_line", position, closest_roadt3d.origin, godot::Color(1.0f, 0.5f, 0.5f), _TICK_DELTA);

}


void PhysicsCar::process_car_car_collision()
{
	return;
}

void PhysicsCar::postprocess_car()
{
	//godot::Object* dd3d = godot::Engine::get_singleton()->get_singleton("DebugDraw3D");
	orientation.orthonormalize();
	gravity_orientation.normalize();
	//dd3d->call("draw_arrow", position, position + orientation[0] * 4, godot::Color(1.0f, 0.0f, 0.0f), 0.25, true, _TICK_DELTA);
	//dd3d->call("draw_arrow", position, position + orientation[1] * 4, godot::Color(0.0f, 1.0f, 0.0f), 0.25, true, _TICK_DELTA);
	//dd3d->call("draw_arrow", position, position + orientation[2] * 4, godot::Color(0.0f, 0.0f, 1.0f), 0.25, true, _TICK_DELTA);
}