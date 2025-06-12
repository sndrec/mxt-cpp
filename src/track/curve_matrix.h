#pragma once

#include "mxt_core/curve.h"
#include "godot_cpp/classes/engine.hpp"

class CurveMatrix
{
public:
	Curve* position_x;
	Curve* position_y;
	Curve* position_z;
	Curve* basis_xx;
	Curve* basis_xy;
	Curve* basis_xz;
	Curve* basis_yx;
	Curve* basis_yy;
	Curve* basis_yz;
	Curve* basis_zx;
	Curve* basis_zy;
	Curve* basis_zz;
	Curve* scale_x;
	Curve* scale_y;
	Curve* scale_z;
	godot::Transform3D get_root_transform_at_time(float in_time) const;
};