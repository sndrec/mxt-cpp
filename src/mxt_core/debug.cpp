#include "debug.hpp"
#include "godot_cpp/core/class_db.hpp"
#include "godot_cpp/classes/engine.hpp"

namespace DEBUG
{
	static int DIP_SWITCH_BITFIELD = 0;

	bool dip_enabled(int in_dip)
	{
		return (DIP_SWITCH_BITFIELD & in_dip) != 0;
	}

	void enable_dip(int in_dip)
	{
		DIP_SWITCH_BITFIELD |= in_dip;
	}

	void disable_dip(int in_dip)
	{
		DIP_SWITCH_BITFIELD &= ~in_dip;
	}

	void disp_text(godot::String in_str, godot::Variant in_var)
	{
		godot::Object* dd2d = godot::Engine::get_singleton()->get_singleton("DebugDraw2D");
		dd2d->call("set_text", in_str, in_var);
	};
}