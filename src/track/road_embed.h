#pragma once

#include "mxt_core/curve.h"

class RoadEmbed
{
public:
	float start_offset;
	float end_offset;
	int embed_type; // todo: change embed_type to use terrain enumerator in enums.h
	Curve* left_border;
	Curve* right_border;
};