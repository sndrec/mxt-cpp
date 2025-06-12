#pragma once

#include "mxt_core/curve.h"

class RoadEmbed
{
public:
	float start_offset;
	float end_offset;
	int embed_type;
	Curve* left_border;
	Curve* right_border;
};