#pragma once

#include "mxt_core/curve.h"
#include "track/road_modulation.h"
#include "track/road_embed.h"

class RoadShape;

class TrackSegment
{
public:
	float segment_length;
	RoadShape* road_shape;
	RoadTransformCurve* curve_matrix;
};

class RoadShape
{
public:
	int num_modulations;
	int num_embeds;
	TrackSegment* owning_segment;
	RoadModulation* road_modulations;
	RoadEmbed* road_embeds;
	Curve* openness;
	virtual godot::Vector3 get_position_at_time(const godot::Vector2& in_t) const;
	virtual godot::Transform3D get_transform_at_time(const godot::Vector2& in_t) const;
	virtual godot::Vector2 find_t_from_relative_pos(const godot::Vector3& in_pos) const;
	godot::Transform3D get_oriented_transform_at_time(const godot::Vector2& in_t) const;
};

class RoadShapeCylinder : public RoadShape
{
public:
	godot::Vector3 get_position_at_time(const godot::Vector2& in_t) const override;
	godot::Transform3D get_transform_at_time(const godot::Vector2& in_t) const override;
	godot::Vector2 find_t_from_relative_pos(const godot::Vector3& in_pos) const override;
};

class RoadShapePipe : public RoadShape
{
public:
	godot::Vector3 get_position_at_time(const godot::Vector2& in_t) const override;
	godot::Transform3D get_transform_at_time(const godot::Vector2& in_t) const override;
	godot::Vector2 find_t_from_relative_pos(const godot::Vector3& in_pos) const override;
};

class RoadShapeCylinderOpen : public RoadShape
{
public:
	godot::Vector3 get_position_at_time(const godot::Vector2& in_t) const override;
	godot::Transform3D get_transform_at_time(const godot::Vector2& in_t) const override;
	godot::Vector2 find_t_from_relative_pos(const godot::Vector3& in_pos) const override;
};

class RoadShapePipeOpen : public RoadShape
{
public:
	godot::Vector3 get_position_at_time(const godot::Vector2& in_t) const override;
	godot::Transform3D get_transform_at_time(const godot::Vector2& in_t) const override;
	godot::Vector2 find_t_from_relative_pos(const godot::Vector3& in_pos) const override;
};