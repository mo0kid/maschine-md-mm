#pragma once

#include "mdMaschineNihiaProtocol.h"
#include "mdMaschineSectionPlayback.h"

namespace mdJucePlugin::maschine
{
	// NIHIA touch-strip LEDs are one-based 63..87, left to right:
	// sixteen native step lamps, five dark separators, four native scale lamps.
	// Keep this independent of pad modes, lightshows and the LCD's page styling.
	inline void setStripLeds(nihia::LedFrame& frame, const md::FrontPanel& panel,
		const md::MachineModel model)
	{
		for(uint8_t index = 63; index <= 87; ++index)
			nihia::setLed(frame, index, nihia::LedColor::Off, 0);
		for(uint8_t step = 0; step < 16; ++step)
		{
			auto color = nihia::LedColor::Off;
			if(model == md::MachineModel::Machinedrum)
				color = panel.getStepLed(step) ? nihia::LedColor::Red : color;
			else
				switch(panel.getMonomachineStepLedColor(step))
				{
				case md::FrontPanel::LedColor::Green: color = nihia::LedColor::Green; break;
				case md::FrontPanel::LedColor::Red: color = nihia::LedColor::Red; break;
				case md::FrontPanel::LedColor::Yellow: color = nihia::LedColor::Yellow; break;
				case md::FrontPanel::LedColor::Off: break;
				}
			nihia::setLed(frame, static_cast<uint8_t>(63 + step), color,
				color == nihia::LedColor::Off ? 0 : 3);
		}
		const auto pages = occupiedScalePages(panel, model);
		for(uint8_t page = 0; page < 4; ++page)
			if(pages & (1u << page))
				nihia::setLed(frame, static_cast<uint8_t>(84 + page), nihia::LedColor::Red, 3);
	}
}
