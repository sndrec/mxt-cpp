#pragma once

#include "godot_cpp/variant/variant.hpp"
namespace DEBUG
{
	extern int DIP_SWITCH_BITFIELD;
	inline bool dip_enabled(int in_dip)
	{
		return (DIP_SWITCH_BITFIELD & in_dip) != 0;
	}
	void enable_dip(int in_dip);
	void disable_dip(int in_dip);
	void disp_text(godot::String in_str, godot::Variant in_var);
}
