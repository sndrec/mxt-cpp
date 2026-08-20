#include "core/debug.hpp"
#include "godot_cpp/core/class_db.hpp"
#include "godot_cpp/classes/engine.hpp"

namespace DEBUG
{
	int DIP_SWITCH_BITFIELD = 0;
	int RAIL_TRACE_CAR_INDEX = -1;
	int RAIL_TRACE_TICK_START = -1;
	int RAIL_TRACE_TICK_END = -1;
	int YAW_TRACE_CAR_INDEX = -1;
	int YAW_TRACE_TICK_START = -1;
	int YAW_TRACE_TICK_END = -1;

	void enable_dip(int in_dip)
	{
		DIP_SWITCH_BITFIELD |= in_dip;
	}

	void disable_dip(int in_dip)
	{
		DIP_SWITCH_BITFIELD &= ~in_dip;
	}

	void set_rail_trace_filter(int car_index, int tick_start, int tick_end)
	{
		RAIL_TRACE_CAR_INDEX = car_index;
		RAIL_TRACE_TICK_START = tick_start;
		RAIL_TRACE_TICK_END = tick_end;
	}

	void set_yaw_trace_filter(int car_index, int tick_start, int tick_end)
	{
		YAW_TRACE_CAR_INDEX = car_index;
		YAW_TRACE_TICK_START = tick_start;
		YAW_TRACE_TICK_END = tick_end;
	}

	void disp_text(godot::String in_str, godot::Variant in_var)
	{
		godot::Object* dd2d = godot::Engine::get_singleton()->get_singleton("DebugDraw2D");
		dd2d->call("set_text", in_str, in_var);
	};
}
