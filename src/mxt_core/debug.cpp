#include "debug.hpp"

namespace DEBUG
{
	static int DIP_SWITCH_BITFIELD = 0;

	bool dip_enabled(int in_dip){
		return (DIP_SWITCH_BITFIELD & in_dip) != 0;
	}

	void enable_dip(int in_dip){
		DIP_SWITCH_BITFIELD |= in_dip;
	}

	void disable_dip(int in_dip){
		DIP_SWITCH_BITFIELD &= ~in_dip;
	}
}