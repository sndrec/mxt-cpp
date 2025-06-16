#pragma once

#include "mxt_core/curve.h"
#include "track/road_modulation.h"
#include "track/road_embed.h"
#include "mxt_core/enums.h"

class RoadShape;

class TrackSegment
{
public:
        float segment_length;
        float left_rail_height;
        float right_rail_height;
        RoadShape* road_shape;
        RoadTransformCurve* curve_matrix;
};

class RoadShape
{
public:
        int num_modulations;
        int num_embeds;
        int shape_type;
        TrackSegment* owning_segment;
	RoadModulation* road_modulations;
	RoadEmbed* road_embeds;
	Curve* openness;
	virtual void get_position_at_time(godot::Vector3 &out_pos, const godot::Vector2& in_t) const;
	virtual void get_transform_at_time(godot::Transform3D &out_transform, const godot::Vector2& in_t) const;
	virtual void find_t_from_relative_pos(godot::Vector2 &out_t, const godot::Vector3& in_pos) const;
	void get_oriented_transform_at_time(godot::Transform3D &out_transform, const godot::Vector2& in_t) const;
};

class RoadShapeCylinder : public RoadShape
{
public:
	void get_position_at_time(godot::Vector3 &out_pos, const godot::Vector2& in_t) const override;
	void get_transform_at_time(godot::Transform3D &out_transform, const godot::Vector2& in_t) const override;
	void find_t_from_relative_pos(godot::Vector2 &out_t, const godot::Vector3& in_pos) const override;
};

class RoadShapePipe : public RoadShape
{
public:
	void get_position_at_time(godot::Vector3 &out_pos, const godot::Vector2& in_t) const override;
	void get_transform_at_time(godot::Transform3D &out_transform, const godot::Vector2& in_t) const override;
	void find_t_from_relative_pos(godot::Vector2 &out_t, const godot::Vector3& in_pos) const override;
};

class RoadShapeCylinderOpen : public RoadShape
{
public:
	void get_position_at_time(godot::Vector3 &out_pos, const godot::Vector2& in_t) const override;
	void get_transform_at_time(godot::Transform3D &out_transform, const godot::Vector2& in_t) const override;
	void find_t_from_relative_pos(godot::Vector2 &out_t, const godot::Vector3& in_pos) const override;
};

class RoadShapePipeOpen : public RoadShape
{
public:
	void get_position_at_time(godot::Vector3 &out_pos, const godot::Vector2& in_t) const override;
	void get_transform_at_time(godot::Transform3D &out_transform, const godot::Vector2& in_t) const override;
	void find_t_from_relative_pos(godot::Vector2 &out_t, const godot::Vector3& in_pos) const override;
};