#pragma once

#include "godot_cpp/classes/engine.hpp"

struct CurveKeyframe
{
	float time; // between 0.0 and 1.0 inclusive
	float value;
	float tangent_in;
	float tangent_out;
};

class Curve
{
public:
	int num_keyframes;
	CurveKeyframe* keyframes;
	Curve(int keyframe_count, CurveKeyframe* in_keyframes)
	{
		keyframes = in_keyframes;
		num_keyframes = keyframe_count;
	}

	float sample(float in_t) const;
};

struct RoadTransformCurveKeyframe
{
	float time; // between 0.0 and 1.0 inclusive
	float value[16];
	float tangent_in[16];
	float tangent_out[16];
};

class RoadTransformCurve
{
public:
	int num_keyframes;
	RoadTransformCurveKeyframe* keyframes;
	RoadTransformCurve(int keyframe_count, RoadTransformCurveKeyframe* in_keyframes)
	{
		keyframes = in_keyframes;
		num_keyframes = keyframe_count;
	}
	godot::Transform3D sample(float in_t) const;
	godot::Transform3D get_keyframe_value(int in_keyframe) const
	{
		RoadTransformCurveKeyframe tar_key = keyframes[in_keyframe];
		godot::Basis transform_basis = godot::Basis(
			tar_key.value[3], tar_key.value[6], tar_key.value[9],
			tar_key.value[4], tar_key.value[7], tar_key.value[10],
			tar_key.value[5], tar_key.value[8], tar_key.value[11]
		);
		return godot::Transform3D(
			transform_basis,
			godot::Vector3(tar_key.value[0], tar_key.value[1], tar_key.value[2])
		).scaled_local(
			godot::Vector3(tar_key.value[12], tar_key.value[13], tar_key.value[14])
		);
	}
};