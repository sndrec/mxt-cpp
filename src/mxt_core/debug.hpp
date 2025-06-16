#pragma once

#include "godot_cpp/variant/variant.hpp"
namespace DEBUG
{
	bool dip_enabled(int in_dip);
	void enable_dip(int in_dip);
	void disable_dip(int in_dip);
	void disp_text(godot::String in_str, godot::Variant in_var);
}