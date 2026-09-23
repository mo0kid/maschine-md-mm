#pragma once

#include "mdLib/mdfrontpanel.h"
#include "mdMaschineNihiaProtocol.h"

namespace mdJucePlugin::maschine
{
	inline nihia::LedColor machinedrumPadColor(const md::FrontPanel& panel,
		const unsigned step, const bool recording, const bool playing,
		const bool gridRecording, const unsigned selectedPage)
	{
		// In normal MD playback the native step lamps are cursor/activity,
		// not an occupied-step map. Do not turn them into a pad playhead.
		if(!recording) return nihia::LedColor::Off;
		const auto cursor = panel.getMachinedrumPlaybackStep();
		if(playing && cursor >= 0 && unsigned(cursor % 16) == step
			&& (!gridRecording || unsigned(cursor / 16) == selectedPage))
			return nihia::LedColor::Red;
		return panel.getStepLed(step) ? nihia::LedColor::Yellow : nihia::LedColor::Off;
	}

	inline uint8_t stepPadBrightness(const nihia::LedColor color)
	{
		return color == nihia::LedColor::Yellow ? 1 : color == nihia::LedColor::Off ? 0
			: color == nihia::LedColor::Red ? 2 : 3;
	}

	// Grid entry uses native red trigs and a yellow cursor; playback/live
	// recording use green trigs and a red/yellow cursor. Never retain a cursor
	// colour over time: that manufactures occupied steps and leaves trails.
	inline nihia::LedColor monomachinePadColor(const md::FrontPanel::LedColor native,
		const bool recording, const bool gridRecording)
	{
		using Native = md::FrontPanel::LedColor;
		using Pad = nihia::LedColor;
		if(native == Native::Off) return Pad::Off;
		if(native == Native::Green) return Pad::Yellow;
		if(gridRecording && native == Native::Red) return Pad::Yellow;
		return recording ? Pad::Red : Pad::White;
	}
}
