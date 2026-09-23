#pragma once

#include "mdLib/mdfrontpanel.h"
#include "mdLib/mdtypes.h"

#include <chrono>
#include <cstdint>
#include <utility>

namespace mdJucePlugin::maschine
{
	inline std::pair<uint8_t, uint8_t> scalePageLed(
		const md::MachineModel _model, const uint8_t _page)
	{
		if(_model == md::MachineModel::Monomachine)
			return {0x27, static_cast<uint8_t>(_page + 4)};
		return _page == 3 ? std::pair<uint8_t, uint8_t>{0x23, 6}
			: std::pair<uint8_t, uint8_t>{0x22, _page};
	}

	// Raw native lamps. Outside grid recording these represent pattern length;
	// in grid recording the baseline is just the selected page. Playback XORs
	// the playing page into that baseline, so these are not an occupancy mask.
	inline uint8_t occupiedScalePages(const md::FrontPanel& _panel,
		const md::MachineModel _model)
	{
		uint8_t mask = 0;
		for(uint8_t page = 0; page < 4; ++page)
		{
			const auto [bank, bit] = scalePageLed(_model, page);
			if((_panel.getLedBankRaw(bank) & (1u << bit)) == 0)
				mask |= static_cast<uint8_t>(1u << page);
		}
		return mask;
	}

	inline bool scaleSetupVisible(const md::FrontPanel& panel, md::MachineModel model)
	{
		// Exact SCALE heading from the stock MD 1.63 / MM 1.32b LCDs.
		constexpr const char* heading[] = {
			"11111011111011111010000011111",
			"10000010000010001010000010000",
			"11111010000011111010000011111",
			"00001010000010001010000010000",
			"11111011111010001011111011111"};
		const unsigned x0 = model == md::MachineModel::Machinedrum ? 32 : 33;
		const unsigned y0 = model == md::MachineModel::Machinedrum ? 17 : 16;
		for(unsigned y = 0; y < 5; ++y)
			for(unsigned x = 0; x < 29; ++x)
				if(panel.getLcdPixel(x0 + x, y0 + y) != (heading[y][x] == '1'))
					return false;
		return true;
	}

	struct SectionDisplayState
	{
		uint8_t enabled = 0;
		int selected = 0;
		uint8_t pulse = 0;

		void update(uint8_t lamps, bool recording, bool playing, bool scaleSetup,
			bool realtimeRecording = false)
		{
			const bool gridRecording = recording && !realtimeRecording;
			lamps &= 0x0f;
			uint8_t lengthMask = 0;
			for(uint8_t i = 0; i < 4; ++i)
				if(lamps & (1u << i)) lengthMask = static_cast<uint8_t>((2u << i) - 1);
			if(scaleSetup || (!recording && !playing))
				enabled = lengthMask;
			else
				enabled |= lengthMask;
			if(gridRecording && !scaleSetup && lamps && !(lamps & (lamps - 1)))
				for(int i = 0; i < 4; ++i)
					if(lamps == (1u << i)) selected = i;
			if(enabled && !(enabled & (1u << selected))) selected = 0;
			const auto baseline = gridRecording ? (1u << selected) : enabled;
			const auto difference = static_cast<uint8_t>((lamps ^ baseline) & enabled);
			// The panel's page lamps carry the playback pulse themselves. In
			// realtime record the transport may have been started from the desktop,
			// leaving the Maschine-local playing flag false.
			pulse = !scaleSetup && difference && !(difference & (difference - 1))
				? difference : 0;
		}
	};

	// Normalize the native lamp phases to a grey/white beat on the playing
	// section. Other enabled sections stay white. The renderer receives only
	// the mask of labels that should be grey, with no second phase inversion.
	struct SectionFlashPhase
	{
		using Clock = std::chrono::steady_clock;
		uint8_t page = 0;
		Clock::time_point lastPulse{};
		bool havePulse = false;
		bool wasPlaying = false;
		uint8_t previousPulse = 0;
		unsigned beats = 0;
		uint8_t nativePage = 0;
		Clock::time_point firstEdge{}, lastEdge{};
		Clock::duration beatDuration = std::chrono::milliseconds(500);
		bool lastEdgeInSecondHalf = true;

		uint8_t update(const uint8_t pulse, const uint8_t enabled,
			const bool playing, const bool scaleSetup, const Clock::time_point now,
			const int knownPage = -1, const bool gridRecording = false,
			const int selectedPage = 0)
		{
			const bool stopped = wasPlaying && !playing;
			const bool started = playing && !wasPlaying;
			wasPlaying = playing;
			if(scaleSetup || stopped)
			{
				page = 0;
				havePulse = false;
				previousPulse = 0;
				beats = 0;
				nativePage = 0;
				return 0;
			}
			if(started)
			{
				page = static_cast<uint8_t>(enabled & -enabled);
				previousPulse = 0;
				beats = 0;
				nativePage = 0;
			}
			// In grid mode the selected lamp flashes inverted, but the OTHER
			// playing-page lamps do not. Thus a section transition can either
			// join two pulses or leave a full beat without a page identity. Track
			// four beats and their observed duration to bridge that gap, instead
			// of waiting for a later pulse to reveal the new page.
			if(pulse)
			{
				if(pulse != previousPulse)
				{
					if(pulse != nativePage || beats >= 4)
					{
						beats = 1;
						firstEdge = now;
					}
					else
					{
						++beats;
						const auto observed = (now - firstEdge) / (beats - 1);
						if(observed >= std::chrono::milliseconds(100)
							&& observed <= std::chrono::seconds(3)) beatDuration = observed;
					}
					lastEdge = now;
					nativePage = page = pulse;
					lastEdgeInSecondHalf = !gridRecording || pulse == (1u << selectedPage);
				}
				lastPulse = now;
				havePulse = true;
			}
			previousPulse = pulse;
			if(beats == 4 && now - lastEdge >= (lastEdgeInSecondHalf ? beatDuration / 2 : beatDuration))
			{
				page = static_cast<uint8_t>(nativePage << 1);
				if(!(page & enabled)) page = static_cast<uint8_t>(enabled & -enabled);
			}
			if(playing && knownPage >= 0 && knownPage < 4)
				page = static_cast<uint8_t>(1u << knownPage);
			// Playback started on the desktop does not set the Maschine transport
			// flag. Native pulses still keep the two flash phases alive in that case.
			if(!playing && (!havePulse || now - lastPulse >= std::chrono::milliseconds(1200)))
				page = 0;
			const bool inverted = !gridRecording || page == (1u << selectedPage);
			return static_cast<uint8_t>(page & enabled & (inverted ? ~pulse : pulse));
		}
	};

	// Both units use a steady RECORD lamp for grid entry and a blinking lamp for live
	// recording. Live recording retains the normal pattern-length page baseline.
	struct SectionRecordingState
	{
		using Clock = std::chrono::steady_clock;
		Clock::time_point lastLit{};
		Clock::time_point lastChange{};
		bool initialized = false;
		bool wasLit = false;
		bool seenLit = false;
		bool active = false;
		bool realtime = false;

		void update(const bool lit, const Clock::time_point now,
			const bool realtimeGesture = false)
		{
			if(!initialized || lit != wasLit)
			{
				if(initialized && wasLit && !lit)
					realtime = true;
				lastChange = now;
				initialized = true;
			}
			wasLit = lit;
			if(lit || realtimeGesture)
			{
				lastLit = now;
				seenLit = true;
			}
			if(realtimeGesture)
			{
				realtime = true;
				lastChange = now;
			}
			active = lit || (seenLit && now - lastLit < std::chrono::milliseconds(1200));
			if(!active || (lit && now - lastChange >= std::chrono::milliseconds(1200)))
				realtime = false;
		}
	};

	inline int monomachinePlayheadStep(const md::FrontPanel& panel)
	{
		int step = -1;
		for(int i = 0; i < 16; ++i)
		{
			const auto color = panel.getMonomachineStepLedColor(i);
			if(color != md::FrontPanel::LedColor::Red
				&& color != md::FrontPanel::LedColor::Yellow)
				continue;
			if(step >= 0)
				return -1; // Ambiguous: fall back to the native page lamp.
			step = i;
		}
		return step;
	}

	// In live recording MM flashes its page lamp at step 3 of each beat. The
	// playhead reaches the section at step 1; use that for the display's beat
	// phase and the page lamp to establish/correct the section number. Grid
	// recording can show stationary red trigs, so it must bypass this predictor.
	struct SectionPlayheadFlash
	{
		int page = -1;
		int previousStep = -1;

		uint8_t update(const int step, const uint8_t nativePulse,
			const uint8_t enabled, const bool recording, const bool playing,
			const bool scaleSetup)
		{
			if(!recording || !playing || scaleSetup || !enabled)
			{
				previousStep = -1;
				page = -1;
				return nativePulse;
			}
			if(step < 0)
			{
				previousStep = -1;
				return nativePulse;
			}
			int count = 0;
			for(int i = 0; i < 4; ++i)
				if(enabled & (1u << i)) count = i + 1;
			if(previousStep >= 0 && step < previousStep && page >= 0)
				page = (page + 1) % count;
			previousStep = step;
			if(step >= 2 && nativePulse
				&& !(nativePulse & (nativePulse - 1)))
				for(int i = 0; i < 4; ++i)
					if(nativePulse == (1u << i)) page = i;
			if(page < 0)
				return nativePulse;
			return (step & 3) < 2 ? static_cast<uint8_t>(1u << page) : 0;
		}
	};

	// Shared by the hardware display worker and the firmware regression test.
	struct MonomachineSectionDisplay
	{
		SectionDisplayState sections;
		SectionRecordingState recording;
		SectionPlayheadFlash playhead;
		SectionFlashPhase flash;
		uint8_t pulse = 0;

		void update(const md::FrontPanel& panel, const bool playing,
			const SectionRecordingState::Clock::time_point now,
			const bool realtimeGesture = false)
		{
			recording.update((panel.getLedBankRaw(0x27) & 1u) == 0, now, realtimeGesture);
			const bool scaleSetup = scaleSetupVisible(panel, md::MachineModel::Monomachine);
			const bool transportRunning = playing || recording.realtime;
			sections.update(occupiedScalePages(panel, md::MachineModel::Monomachine),
				recording.active, transportRunning, scaleSetup, recording.realtime);
			playhead.update(monomachinePlayheadStep(panel), sections.pulse,
				sections.enabled, recording.realtime, transportRunning, scaleSetup);
			pulse = flash.update(sections.pulse, sections.enabled, transportRunning,
				scaleSetup, now, recording.realtime ? playhead.page : -1,
				recording.active && !recording.realtime, sections.selected);
		}
	};

	struct MachinedrumSectionDisplay
	{
		SectionDisplayState sections;
		SectionRecordingState recording;
		SectionFlashPhase flash;
		uint8_t pulse = 0; // Final grey-label mask, exactly as sent to the LCD renderer.

		void update(const md::FrontPanel& panel, const bool playing,
			const SectionRecordingState::Clock::time_point now,
			const bool realtimeGesture = false)
		{
			recording.update(panel.getModeLed(md::FrontPanel::ModeLed::Record), now, realtimeGesture);
			const bool scaleSetup = scaleSetupVisible(panel, md::MachineModel::Machinedrum);
			const bool transportRunning = playing || recording.realtime;
			sections.update(occupiedScalePages(panel, md::MachineModel::Machinedrum),
				recording.active, transportRunning, scaleSetup, recording.realtime);
			pulse = flash.update(sections.pulse, sections.enabled, transportRunning, scaleSetup, now,
				-1, recording.active && !recording.realtime, sections.selected);
		}
	};
}
