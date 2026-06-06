#pragma once

#include <cstdint>
#include "godot_cpp/variant/variant.hpp"
namespace DEBUG
{
	extern int DIP_SWITCH_BITFIELD;
	extern int RAIL_TRACE_CAR_INDEX;
	extern int RAIL_TRACE_TICK_START;
	extern int RAIL_TRACE_TICK_END;
	inline bool dip_enabled(int in_dip)
	{
		return (DIP_SWITCH_BITFIELD & in_dip) != 0;
	}
	inline bool rail_trace_filter_matches(int car_index, uint32_t tick)
	{
		if (RAIL_TRACE_CAR_INDEX >= 0 && RAIL_TRACE_CAR_INDEX != car_index) {
			return false;
		}
		if (RAIL_TRACE_TICK_START >= 0 && tick < static_cast<uint32_t>(RAIL_TRACE_TICK_START)) {
			return false;
		}
		if (RAIL_TRACE_TICK_END >= 0 && tick > static_cast<uint32_t>(RAIL_TRACE_TICK_END)) {
			return false;
		}
		return true;
	}
	void enable_dip(int in_dip);
	void disable_dip(int in_dip);
	void set_rail_trace_filter(int car_index, int tick_start, int tick_end);
	void disp_text(godot::String in_str, godot::Variant in_var);
}
