#pragma once

#include "mxt_core/curve.h"
#include "track/road_modulation.h"
#include "track/road_embed.h"
#include "mxt_core/enums.h"
#include <algorithm>

struct SegmentAABB
{
	godot::Vector3 position;
	godot::Vector3 size;
	inline void expand_to(const godot::Vector3 &p)
	{
	float min_x = std::min(position.x, p.x);
	float min_y = std::min(position.y, p.y);
	float min_z = std::min(position.z, p.z);
	float max_x = std::max(position.x + size.x, p.x);
	float max_y = std::max(position.y + size.y, p.y);
	float max_z = std::max(position.z + size.z, p.z);
	position = godot::Vector3(min_x, min_y, min_z);
	size = godot::Vector3(max_x - min_x, max_y - min_y, max_z - min_z);
	}
	inline bool has_point(const godot::Vector3 &p) const
	{
	return p.x >= position.x && p.x <= position.x + size.x &&
	       p.y >= position.y && p.y <= position.y + size.y &&
	       p.z >= position.z && p.z <= position.z + size.z;
	}
};

class RoadShape;

class TrackSegment
{
public:
float segment_length;
float left_rail_height;
float right_rail_height;
RoadShape* road_shape;
RoadTransformCurve* curve_matrix;
SegmentAABB bounds;
int first_checkpoint;
int checkpoint_count;
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