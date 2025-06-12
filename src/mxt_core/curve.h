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
	void sample(godot::Transform3D &out_transform, float in_t) const;
	void get_keyframe_value(godot::Transform3D &out_transform, int in_keyframe) const
	{
		out_transform.basis.set(keyframes[in_keyframe].value[3], keyframes[in_keyframe].value[6], keyframes[in_keyframe].value[9],
			keyframes[in_keyframe].value[4], keyframes[in_keyframe].value[7], keyframes[in_keyframe].value[10],
			keyframes[in_keyframe].value[5], keyframes[in_keyframe].value[8], keyframes[in_keyframe].value[11]);
		out_transform.origin.x = keyframes[in_keyframe].value[0];
		out_transform.origin.y = keyframes[in_keyframe].value[1];
		out_transform.origin.z = keyframes[in_keyframe].value[2];
		out_transform.basis.scale_local(godot::Vector3(keyframes[in_keyframe].value[12], keyframes[in_keyframe].value[13], keyframes[in_keyframe].value[14]));
	}
};