#pragma once

class MXTPlane
{
public:
	godot::Vector3 normal;
	float d;
	MXTPlane(godot::Vector3 in_normal, godot::Vector3 in_point)
	{
		normal = in_normal;
		d = normal.dot(in_point);
	};
	MXTPlane(godot::Vector3 in_normal, float in_d)
	{
		normal = in_normal;
		d = in_d;
	};
	bool is_point_above(godot::Vector3 in_point) const
	{
		return in_point.dot(normal) > d;
	};
	godot::Vector3 project_point(godot::Vector3 in_point) const
	{
		return in_point - normal * (normal.dot(in_point) - d);
	};
	float distance_to(godot::Vector3 in_point) const
	{
		return normal.dot(in_point) - d;
	};
	godot::Vector3 intersect_segment(godot::Vector3 p_begin, godot::Vector3 p_end) const
	{
		godot::Vector3 segment = p_begin - p_end;
		float den = normal.dot(segment);

		//printf("den is %i\n",den);
		if (den < (float)CMP_EPSILON && den >(float)-CMP_EPSILON) {
			return godot::Vector3(0.0f, 0.0f, 0.0f);
		};

		float dist = (normal.dot(p_begin) - d) / den;
		//printf("dist is %i\n",dist);

		if (dist < (float)-CMP_EPSILON || dist >(1.0f + (float)CMP_EPSILON)) {
			return godot::Vector3(0.0f, 0.0f, 0.0f);
		};

		dist = -dist;
		godot::Vector3 p_intersection = p_begin + segment * dist;

		return p_intersection;
	};
};