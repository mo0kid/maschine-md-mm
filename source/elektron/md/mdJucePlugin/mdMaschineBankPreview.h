#pragma once

#include "mdLib/mdfrontpanel.h"

namespace mdJucePlugin::maschine
{
	// MD 1.63 displays this BANK heading after releasing a bank key. The
	// countdown at y=37..41 changes while the overlay is still active.
	inline bool mdBankOverlayVisible(const md::FrontPanel& panel)
	{
		constexpr const char* heading[] = {
			"11111011111011111010001",
			"10001010001010001010010",
			"11110011111010001011100",
			"10001010001010001010010",
			"11111010001010001010001"};
		for(unsigned y = 0; y < 5; ++y)
			for(unsigned x = 0; x < 23; ++x)
				if(panel.getLcdPixel(47 + x, 23 + y) != (heading[y][x] == '1'))
					return false;
		for(unsigned x = 30; x <= 96; ++x)
			if(!panel.getLcdPixel(x, 18) || !panel.getLcdPixel(x, 44))
				return false;
		return true;
	}
}
