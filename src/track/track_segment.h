#pragma once

#include "mxt_core/curve.h"
#include "track/road_modulation.h"
#include "track/road_embed.h"
#include "mxt_core/enums.h"

class RoadShape;

struct TrackEdgeRailSide
{
	SimVec3 pos;
	SimVec3 rail_n;
	SimVec3 up_n;
	SimVec3 forward_n;
	float height;
};

class TrackSegment
{
public:
        float segment_length;
        float left_rail_height;
        float right_rail_height;
        SimAABB bounds;
        int checkpoint_start;
        int checkpoint_run_length;
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
	virtual void get_position_at_time(SimVec3 &out_pos, const SimVec2& in_t) const;
	virtual void get_local_surface_at_time(
		SimVec3 &out_pos,
		SimVec3 &out_dpos_dx,
		SimVec3 &out_dpos_dy,
		const SimVec2& in_t) const;
	//virtual void get_transform_at_time(SimTransform &out_transform, const SimVec2& in_t) const;
	virtual void find_t_from_relative_pos(SimVec2 &out_t, const SimVec3& in_pos) const;
	void get_oriented_transform_at_time_presampled(
		SimTransform &out_transform,
		const SimVec2& in_t,
		const RoadTransform& root,
		const RoadTransform& root_derivative) const;
	void get_oriented_transform_at_time(SimTransform &out_transform, const SimVec2& in_t) const;
	void get_oriented_transform_at_time4(SimTransform out_transform[4], const SimVec2 in_t[4]) const;
	bool supports_edge_rails() const;
	void get_edge_rail_sides(
		TrackEdgeRailSide out_sides[2],
		float road_y,
		const SimVec3& interior_reference,
		const RoadTransform& root,
		const RoadTransform& root_derivative,
		float left_height,
		float right_height) const;
};

class RoadShapeCylinder : public RoadShape
{
public:
	void get_position_at_time(SimVec3 &out_pos, const SimVec2& in_t) const override;
	void get_local_surface_at_time(
		SimVec3 &out_pos,
		SimVec3 &out_dpos_dx,
		SimVec3 &out_dpos_dy,
		const SimVec2& in_t) const override;
	//void get_transform_at_time(SimTransform &out_transform, const SimVec2& in_t) const override;
	void find_t_from_relative_pos(SimVec2 &out_t, const SimVec3& in_pos) const override;
};

class RoadShapePipe : public RoadShape
{
public:
	void get_position_at_time(SimVec3 &out_pos, const SimVec2& in_t) const override;
	void get_local_surface_at_time(
		SimVec3 &out_pos,
		SimVec3 &out_dpos_dx,
		SimVec3 &out_dpos_dy,
		const SimVec2& in_t) const override;
	//void get_transform_at_time(SimTransform &out_transform, const SimVec2& in_t) const override;
	void find_t_from_relative_pos(SimVec2 &out_t, const SimVec3& in_pos) const override;
};

class RoadShapeCylinderOpen : public RoadShape
{
public:
	void get_position_at_time(SimVec3 &out_pos, const SimVec2& in_t) const override;
	void get_local_surface_at_time(
		SimVec3 &out_pos,
		SimVec3 &out_dpos_dx,
		SimVec3 &out_dpos_dy,
		const SimVec2& in_t) const override;
	//void get_transform_at_time(SimTransform &out_transform, const SimVec2& in_t) const override;
	void find_t_from_relative_pos(SimVec2 &out_t, const SimVec3& in_pos) const override;
};

class RoadShapePipeOpen : public RoadShape
{
public:
        void get_position_at_time(SimVec3 &out_pos, const SimVec2& in_t) const override;
        void get_local_surface_at_time(
		SimVec3 &out_pos,
		SimVec3 &out_dpos_dx,
		SimVec3 &out_dpos_dy,
		const SimVec2& in_t) const override;
        //void get_transform_at_time(SimTransform &out_transform, const SimVec2& in_t) const override;
        void find_t_from_relative_pos(SimVec2 &out_t, const SimVec3& in_pos) const override;
};

class RoadShapeRoundedRect : public RoadShape
{
public:
        Curve* width;
        Curve* height;
        Curve* radius;
        void get_position_at_time(SimVec3 &out_pos, const SimVec2& in_t) const override;
        void get_local_surface_at_time(
		SimVec3 &out_pos,
		SimVec3 &out_dpos_dx,
		SimVec3 &out_dpos_dy,
		const SimVec2& in_t) const override;
        void find_t_from_relative_pos(SimVec2 &out_t, const SimVec3& in_pos) const override;
};

class RoadShapeRoundedRectOpen : public RoadShapeRoundedRect
{
public:
        // Seam rotation curve specific to open rounded rect; interpreted in [-1,1] domain
        Curve* open_rotation;
        void get_position_at_time(SimVec3 &out_pos, const SimVec2& in_t) const override;
        void get_local_surface_at_time(
		SimVec3 &out_pos,
		SimVec3 &out_dpos_dx,
		SimVec3 &out_dpos_dy,
		const SimVec2& in_t) const override;
        void find_t_from_relative_pos(SimVec2 &out_t, const SimVec3& in_pos) const override;
};

