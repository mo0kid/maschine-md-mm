#include "mdMaschineScreenRenderer.h"
#include "mdMaschineSectionPlayback.h"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>

namespace
{
	using Renderer = mdJucePlugin::maschine::ScreenRenderer;

	void expect(const bool _condition, const char* const _message)
	{
		if(_condition)
			return;
		std::cerr << "mdMaschineScreenRendererTest: " << _message << '\n';
		std::exit(1);
	}

	md::FrontPanel panelWithTopLeftPixel()
	{
		md::FrontPanel panel;
		const uint8_t tile[] = {0x10, 0x00, 0x01, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00};
		panel.processBytes(tile, sizeof(tile));
		return panel;
	}

	md::FrontPanel diagnosticPanel()
	{
		md::FrontPanel panel;
		for(uint8_t half = 0; half < 2; ++half)
			for(uint8_t page = 0; page < 8; ++page)
				for(uint8_t column = 0; column < 64; column += 8)
				{
					panel.processByte(static_cast<uint8_t>(0x10 | (half << 3) | page));
					panel.processByte(column);
					for(uint8_t i = 0; i < 8; ++i)
						panel.processByte(static_cast<uint8_t>(
							((column + i + page * 3 + half * 7) % 17) < 4
								? 0x7e : 0x00));
				}
		return panel;
	}

	uint16_t pixel(const Renderer::Frame& _frame, const unsigned _x,
		const unsigned _y)
	{
		return _frame[static_cast<size_t>(_y) * Renderer::g_width + _x];
	}

	bool labelHasColor(const Renderer::Frame& _frame, const unsigned _x,
		const uint16_t _color)
	{
		for(unsigned y = 11; y < 19; ++y)
			for(unsigned x = _x; x < _x + 8; ++x)
				if(pixel(_frame, x, y) == _color)
					return true;
		return false;
	}

	void checkLcdGeometryAndPalette()
	{
		const md::FrontPanel blank;
		const auto lit = panelWithTopLeftPixel();
		const auto blankMd = Renderer::render(blank, md::MachineModel::Machinedrum,
			md::MachineModel::Machinedrum, false);
		const auto litMd = Renderer::render(lit, md::MachineModel::Machinedrum,
			md::MachineModel::Machinedrum, false);
		const auto blankMm = Renderer::render(blank, md::MachineModel::Monomachine,
			md::MachineModel::Machinedrum, false);

		expect(blankMd.size() == static_cast<size_t>(480 * 272),
			"frame geometry changed");
		expect(pixel(blankMd, Renderer::g_lcdX, Renderer::g_lcdY)
			== Renderer::lcdOffColor(md::MachineModel::Machinedrum),
			"Machinedrum LCD background color is wrong");
		expect(pixel(blankMm, Renderer::g_lcdX, Renderer::g_lcdY)
			== Renderer::lcdOffColor(md::MachineModel::Monomachine),
			"Monomachine LCD background color is wrong");
		for(unsigned y = 0; y < Renderer::g_lcdScale; ++y)
			for(unsigned x = 0; x < Renderer::g_lcdScale; ++x)
				expect(pixel(litMd, Renderer::g_lcdX + x,
					Renderer::g_lcdY + y)
					== Renderer::lcdOnColor(md::MachineModel::Machinedrum),
					"lit LCD pixel was not expanded to a crisp 3x3 block");
		expect(pixel(litMd, Renderer::g_lcdX + Renderer::g_lcdScale,
			Renderer::g_lcdY) == Renderer::lcdOffColor(md::MachineModel::Machinedrum),
			"LCD pixel scaling bled into its neighbour");
	}

	void checkDirtyBounds()
	{
		const md::FrontPanel blank;
		const auto lit = panelWithTopLeftPixel();
		const auto before = Renderer::render(blank, md::MachineModel::Machinedrum,
			md::MachineModel::Machinedrum, false);
		const auto after = Renderer::render(lit, md::MachineModel::Machinedrum,
			md::MachineModel::Machinedrum, false);
		expect(Renderer::dirtyBounds(before, before).empty(),
			"identical frames produced a dirty rectangle");
		const auto dirty = Renderer::dirtyBounds(before, after);
		expect(dirty.x == Renderer::g_lcdX && dirty.y == Renderer::g_lcdY
			&& dirty.width == Renderer::g_lcdScale
			&& dirty.height == Renderer::g_lcdScale,
			"one LCD pixel did not produce its exact scaled dirty rectangle");
	}

	void checkFocusAndTransportOverlays()
	{
		const md::FrontPanel panel;
		const auto focused = Renderer::render(panel, md::MachineModel::Machinedrum,
			md::MachineModel::Machinedrum, false);
		const auto unfocused = Renderer::render(panel, md::MachineModel::Machinedrum,
			md::MachineModel::Monomachine, false);
		const auto playing = Renderer::render(panel, md::MachineModel::Machinedrum,
			md::MachineModel::Machinedrum, true);
		const auto mixer = Renderer::render(panel, md::MachineModel::Machinedrum,
			md::MachineModel::Machinedrum, false, true);

		expect(pixel(focused, Renderer::g_lcdX, Renderer::g_lcdY)
			== pixel(unfocused, Renderer::g_lcdX, Renderer::g_lcdY),
			"focus changed the emulated LCD contents");
		expect(!Renderer::dirtyBounds(focused, unfocused).empty(),
			"focus change did not update the status overlays");
		expect(Renderer::dirtyBounds(focused, playing).empty(),
			"transport state unexpectedly changed the display header");
		expect(!Renderer::dirtyBounds(focused, mixer).empty(),
			"MIXER did not replace the encoder labels");
	}

	void checkPanelLedOverlays()
	{
		md::FrontPanel mdPanel;
		const auto mdOff = Renderer::render(mdPanel,
			md::MachineModel::Machinedrum, md::MachineModel::Machinedrum, false);
		// The centre stroke of the first character in the 1:4 label.
		expect(pixel(mdOff, 230, 11) == 0x3186,
			"inactive Machinedrum scale label was not grey");
		const auto mdSelectedStopped = Renderer::render(mdPanel,
			md::MachineModel::Machinedrum, md::MachineModel::Machinedrum, false,
			false, 0);
		const auto mdSelectedPlaying = Renderer::render(mdPanel,
			md::MachineModel::Machinedrum, md::MachineModel::Machinedrum, true,
			false, 0, 0x01, true);
		expect(pixel(mdSelectedStopped, 230, 11) == 0x3186,
			"selected page was highlighted outside record mode");
		expect(pixel(mdSelectedPlaying, 230, 11) == 0x3186,
			"playing selected page did not flash grey during record");
		mdPanel.processByte(0x24);
		mdPanel.processByte(0xfe);
		const auto mdOn = Renderer::render(mdPanel,
			md::MachineModel::Machinedrum, md::MachineModel::Machinedrum, false);
		expect(Renderer::dirtyBounds(mdOff, mdOn).x < Renderer::g_lcdX,
			"Machinedrum sound indicator was not drawn beside the LCD");
		mdPanel.processByte(0x22);
		mdPanel.processByte(0xfe);
		const auto mdPageOn = Renderer::render(mdPanel,
			md::MachineModel::Machinedrum, md::MachineModel::Machinedrum, false,
			false, 0);
		expect(!Renderer::dirtyBounds(mdOn, mdPageOn).empty(),
			"Machinedrum 1/4 LED was not drawn in the header");
		expect(pixel(mdPageOn, 230, 11) == 0xffff,
			"occupied Machinedrum page was not white outside record mode");

		md::FrontPanel mmPanel;
		const auto mmOff = Renderer::render(mmPanel,
			md::MachineModel::Monomachine, md::MachineModel::Monomachine, false);
		expect(pixel(mmOff, 230, 11) == 0x3186,
			"inactive Monomachine scale label was not grey");
		mmPanel.processByte(0x27);
		mmPanel.processByte(0xef);
		mmPanel.processByte(0x25);
		mmPanel.processByte(0xfe);
		const auto mmOn = Renderer::render(mmPanel,
			md::MachineModel::Monomachine, md::MachineModel::Monomachine, false,
			false, 0);
		expect(!Renderer::dirtyBounds(mmOff, mmOn).empty(),
			"Monomachine 1/4 LED was not drawn in the header");
		expect(Renderer::dirtyBounds(mmOff, mmOn).x >= Renderer::g_lcdX,
			"Monomachine track LED was not drawn beside the LCD");
		expect(pixel(mmOn, 230, 11) == 0xffff,
			"occupied Monomachine page was not white outside record mode");
	}

	void checkPatternLengthSections()
	{
		using mdJucePlugin::maschine::occupiedScalePages;
		md::FrontPanel mm;
		mm.processByte(0x27);
		mm.processByte(0xcf); // 32 steps: first two page lamps lit.
		expect(occupiedScalePages(mm, md::MachineModel::Monomachine) == 0x03,
			"MM occupied pages did not follow pattern length");
		const auto mmStopped = Renderer::render(mm,
			md::MachineModel::Monomachine, md::MachineModel::Monomachine,
			false, false, 0);
		expect(pixel(mmStopped, 230, 11) == 0xffff,
			"focused MM page was not white outside record mode");
		const auto mmRecording = Renderer::render(mm,
			md::MachineModel::Monomachine, md::MachineModel::Monomachine,
			false, false, 0, 0, true);
		expect(pixel(mmRecording, 230, 11) == 0xffff
			&& pixel(mmRecording, 228, 24) == 0xffff,
			"focused MM page was not white and underlined during record");
		const auto mmRecordPulse = Renderer::render(mm,
			md::MachineModel::Monomachine, md::MachineModel::Monomachine,
			true, false, 0, 0x01, true);
		expect(pixel(mmRecordPulse, 230, 11) == 0x3186
			&& pixel(mmRecordPulse, 270, 11) == 0xffff,
			"MM record pulse must dim only the playing page to grey");
		expect(pixel(mmStopped, 270, 11) == 0xffff,
			"occupied MM page with no trigs was not white");
		expect(labelHasColor(mmStopped, 308, 0x3186)
			&& labelHasColor(mmStopped, 348, 0x3186),
			"MM pages beyond pattern length were not grey");
		const auto mmPlayingSecond = Renderer::render(mm,
			md::MachineModel::Monomachine, md::MachineModel::Monomachine,
			true, false, 0, 0x02);
		expect(pixel(mmPlayingSecond, 270, 11) == 0x3186,
			"playing MM occupied page did not flash grey");
		md::FrontPanel md;
		md.processByte(0x22);
		md.processByte(0xfc); // 32 steps: first two page lamps lit.
		md.processByte(0x23);
		md.processByte(0xff);
		expect(occupiedScalePages(md, md::MachineModel::Machinedrum) == 0x03,
			"MD occupied pages did not follow pattern length");
		const auto mdStopped = Renderer::render(md,
			md::MachineModel::Machinedrum, md::MachineModel::Machinedrum,
			false, false, 0);
		expect(pixel(mdStopped, 230, 11) == 0xffff,
			"focused MD page was not white outside record mode");
		const auto mdRecording = Renderer::render(md,
			md::MachineModel::Machinedrum, md::MachineModel::Machinedrum,
			false, false, 0, 0, true);
		expect(pixel(mdRecording, 230, 11) == 0xffff
			&& pixel(mdRecording, 228, 24) == 0xffff,
			"focused MD page was not white and underlined during record");
		const auto mdRecordPulse = Renderer::render(md,
			md::MachineModel::Machinedrum, md::MachineModel::Machinedrum,
			true, false, 0, 0x01, true);
		expect(pixel(mdRecordPulse, 230, 11) == 0x3186
			&& pixel(mdRecordPulse, 270, 11) == 0xffff,
			"MD record pulse must dim only the playing page to grey");
		expect(pixel(mdStopped, 270, 11) == 0xffff,
			"occupied MD page with no trigs was not white");
		expect(labelHasColor(mdStopped, 308, 0x3186)
			&& labelHasColor(mdStopped, 348, 0x3186),
			"MD pages beyond pattern length were not grey");
	}

	void checkRecordingWithDifferentSelectedAndPlayingSections()
	{
		for(const auto model : {md::MachineModel::Machinedrum,
			md::MachineModel::Monomachine})
		{
			md::FrontPanel panel;
			const auto setPages = [&](const uint8_t mask)
			{
				panel.processByte(model == md::MachineModel::Machinedrum ? 0x22 : 0x27);
				panel.processByte(static_cast<uint8_t>(~(model == md::MachineModel::Machinedrum
					? mask : mask << 4)));
			};
			// Switch focus in a two-bar pattern while playback moves independently.
			mdJucePlugin::maschine::SectionDisplayState state;
			state.update(3, false, false, false);
			for(const bool record : {false, true})
			for(const int selected : {0, 1, 0, 1})
			{
				state.update(static_cast<uint8_t>(1u << selected), true, false, false);
				for(const uint8_t pulse : {0, 1, 0, 2, 0, 2, 0, 1, 0})
				{
					const auto lamps = static_cast<uint8_t>((record ? 1u << selected : 3u) ^ pulse);
					setPages(lamps);
					state.update(lamps, record, true, false);
					expect(state.enabled == 3 && state.selected == selected && state.pulse == pulse,
						"native recording lamps confused enabled, selected or playing sections");
					const auto frame = Renderer::render(panel, model, model, true,
						false, state.selected, state.pulse, record, state.enabled);
					expect(pixel(frame, 230, 11) == (pulse == 1 ? 0x3186 : 0xffff),
						"first enabled bar did not flash white/grey independently of focus");
					expect(pixel(frame, 270, 11) == (pulse == 2 ? 0x3186 : 0xffff),
						"unselected playing bar must flash white/grey in both modes");
					expect(pixel(frame, 228, 24) == (selected == 0 ? 0xffff : 0x1082)
						&& pixel(frame, 268, 24) == (selected == 1 ? 0xffff : 0x1082),
						"underline did not stay on the focused section through playback");
					expect(labelHasColor(frame, 308, 0x3186)
						&& labelHasColor(frame, 348, 0x3186),
						"unused sections changed colour during playback");
				}
			}
		}
	}

	void checkLiveAndGridRecordingDisplay()
	{
		using Display = mdJucePlugin::maschine::MonomachineSectionDisplay;
		const auto start = mdJucePlugin::maschine::SectionRecordingState::Clock::time_point{};
		md::FrontPanel panel;
		const auto setPanel = [&](uint8_t pages, bool record, int redStep)
		{
			panel.processByte(0x27);
			panel.processByte(static_cast<uint8_t>(~((pages << 4) | (record ? 1 : 0))));
			for(int bank = 0; bank < 4; ++bank)
			{
				panel.processByte(static_cast<uint8_t>(0x20 + bank));
				panel.processByte(static_cast<uint8_t>(redStep / 4 == bank
					? ~(2u << (2 * (redStep % 4))) : 0xff));
			}
		};
		Display grid;
		setPanel(15, false, 2);
		grid.update(panel, false, start);
		setPanel(2, true, 2);
		grid.update(panel, false, start + std::chrono::milliseconds(50));
		setPanel(3, true, 2);
		grid.update(panel, true, start + std::chrono::milliseconds(100));
		expect(grid.pulse == 1 && grid.sections.selected == 1,
			"a stationary red trig in grid record suppressed the native page flash");

		for(const bool hardwareGesture : {false, true})
		{
			Display live;
			setPanel(15, false, 0);
			live.update(panel, false, start);
			for(unsigned tick = 0; tick < 128; ++tick)
			{
				const unsigned step = tick % 16;
				const unsigned page = (tick / 16) % 4;
				const auto nativePulse = step % 4 >= 2 ? 1u << page : 0;
				setPanel(static_cast<uint8_t>(15 ^ nativePulse), tick % 8 < 4, step);
				live.update(panel, hardwareGesture,
					start + std::chrono::milliseconds(125 * (tick + 1)),
					hardwareGesture && tick == 0);
				if(tick < 8) continue; // Allow detection of a desktop-started RECORD blink.
				const auto expected = step % 4 < 2 ? 1u << page : 0;
				expect(live.recording.realtime && live.sections.enabled == 15
					&& live.pulse == expected,
					"four-section live recording lost or delayed the displayed page flash");
				const auto frame = Renderer::render(panel, md::MachineModel::Monomachine,
					md::MachineModel::Monomachine, true, false, live.sections.selected,
					live.pulse, live.recording.active, live.sections.enabled);
				for(unsigned label = 0; label < 4; ++label)
					expect(labelHasColor(frame, 228 + label * 40,
						(expected & (1u << label)) ? 0x3186 : 0xffff),
						"live-record page pulse did not reach the rendered Maschine LCD");
			}
		}
	}

	void checkReversedFlashPhaseOnBothDisplays()
	{
		using Phase = mdJucePlugin::maschine::SectionFlashPhase;
		const auto start = Phase::Clock::time_point{};
		for(const auto model : {md::MachineModel::Machinedrum, md::MachineModel::Monomachine})
		{
			Phase phase;
			const md::FrontPanel panel;
			const uint8_t pulses[] = {0, 1, 0, 1, 2, 0, 2, 0};
			const uint8_t expectedDim[] = {1, 0, 1, 0, 0, 2, 0, 2};
			for(unsigned tick = 0; tick < std::size(pulses); ++tick)
			{
				const auto dim = phase.update(pulses[tick], 3, true, false,
					start + std::chrono::milliseconds(tick * 100));
				expect(dim == expectedDim[tick], "playing-section flash phase was not reversed");
				const auto frame = Renderer::render(panel, model, model, true,
					false, 0, dim, true, 3);
				for(unsigned page = 0; page < 4; ++page)
					expect(labelHasColor(frame, 228 + 40 * page,
						page >= 2 || (dim & (1u << page)) ? 0x3186 : 0xffff),
						"phase reversal changed a non-playing or unused section");
				expect(pixel(frame, 228, 24) == 0xffff,
					"phase reversal changed the focused section underline");
			}
			expect(phase.update(0, 3, false, false, start + std::chrono::seconds(1)) == 0,
				"stopped playback left an enabled section grey");
		}
		Phase desktop;
		desktop.update(1, 3, false, false, start);
		expect(desktop.update(0, 3, false, false, start + std::chrono::milliseconds(100)) == 1,
			"desktop-started playback did not reverse the flash phase");
		expect(desktop.update(0, 3, false, false, start + std::chrono::seconds(2)) == 0,
			"desktop playback left a stale dark section after stopping");
		Phase live;
		live.update(1, 3, true, false, start, 0);
		expect(live.update(0, 3, true, false, start + std::chrono::milliseconds(100), 1) == 2,
			"known live-record playhead did not move the reversed flash to the new section");
	}

	void checkMonomachinePageFlashStartsAtFirstStep()
	{
		md::FrontPanel panel;
		panel.processByte(0x20);
		panel.processByte(0xf9); // red playhead on step 1, green trig on step 2
		expect(mdJucePlugin::maschine::monomachinePlayheadStep(panel) == 0,
			"MM playhead was confused with an occupied trig");
		panel.processByte(0x20);
		panel.processByte(0xd9); // second red lamp makes the cursor ambiguous
		expect(mdJucePlugin::maschine::monomachinePlayheadStep(panel) == -1,
			"ambiguous MM playhead should use the native page lamp");
		mdJucePlugin::maschine::SectionPlayheadFlash flash;
		expect(flash.update(2, 1, 3, true, true, false) == 0,
			"page lamp at step 3 should only synchronize the page number");
		expect(flash.update(3, 1, 3, true, true, false) == 0,
			"page flash remained two steps late");
		expect(flash.update(4, 0, 3, true, true, false) == 1,
			"first page did not flash at step 5");
		expect(flash.update(14, 1, 3, true, true, false) == 0,
			"page lamp overrode the playhead phase");
		expect(flash.update(15, 1, 3, true, true, false) == 0,
			"page lamp kept the old page flashing before the wrap");
		expect(flash.update(0, 0, 3, true, true, false) == 2,
			"second section did not flash as soon as step 1 began");
		expect(flash.update(1, 0, 3, true, true, false) == 2,
			"second section flash ended before step 2");
		expect(flash.update(2, 2, 3, true, true, false) == 0,
			"late native page lamp restarted the flash at step 3");
		expect(flash.update(-1, 2, 3, true, true, false) == 2,
			"ambiguous playhead did not fall back to the native lamp");
		expect(flash.update(0, 0, 3, false, true, false) == 0,
			"playhead flash continued after leaving record mode");
	}

	void checkSectionBoundaryFrames()
	{
		using namespace mdJucePlugin::maschine;
		for(const auto model : {md::MachineModel::Machinedrum, md::MachineModel::Monomachine})
		for(const unsigned count : {2u, 4u})
		for(const unsigned bpm : {60u, 125u, 240u})
		for(const unsigned mode : {0u, 1u, 2u}) // playback, grid record, live record
		{
			MachinedrumSectionDisplay mdDisplay;
			MonomachineSectionDisplay mmDisplay;
			md::FrontPanel panel;
			const uint8_t enabled = (1u << count) - 1;
			const auto setPanel = [&](uint8_t lamps, bool record, int step)
			{
				for(uint8_t page = 0; page < 4; ++page)
				{
					const auto [bank, bit] = scalePageLed(model, page);
					const auto raw = panel.getLedBankRaw(bank);
					panel.processByte(bank);
					panel.processByte((lamps & (1u << page)) ? raw & ~(1u << bit) : raw | (1u << bit));
				}
				const uint8_t bank = model == md::MachineModel::Machinedrum ? 0x23 : 0x27;
				const uint8_t bit = model == md::MachineModel::Machinedrum ? 4 : 0;
				const auto raw = panel.getLedBankRaw(bank);
				panel.processByte(bank);
				panel.processByte(record ? raw & ~(1u << bit) : raw | (1u << bit));
				if(model == md::MachineModel::Monomachine)
					for(int b = 0; b < 4; ++b) {
						panel.processByte(0x20 + b);
						panel.processByte(step / 4 == b ? ~(2u << (2 * (step % 4))) : 0xff);
					}
			};
			const auto start = SectionFlashPhase::Clock::time_point{};
			setPanel(enabled, false, 2);
			mdDisplay.update(panel, false, start);
			mmDisplay.update(panel, false, start);
			if(mode == 1) {
				setPanel(2, true, 2); // Edit page 2, regardless of the playing page.
				mdDisplay.update(panel, false, start);
				mmDisplay.update(panel, false, start);
			}
			for(unsigned tick = 0; tick < count * 16 * 2; ++tick)
			{
				const unsigned step = tick % 16;
				const unsigned page = (tick / 16) % count;
				const bool inverted = mode != 1 || page == 1;
				const auto nativePulse = (inverted ? step % 4 >= 2 : step % 4 < 2) ? 1u << page : 0;
				const auto baseline = mode == 1 ? 2u : enabled;
				setPanel(baseline ^ nativePulse, mode == 1 || (mode == 2 && tick % 8 < 4), mode == 2 ? step : 2);
				const auto now = start + std::chrono::microseconds((tick + 1) * 15000000ull / bpm);
				mdDisplay.update(panel, true, now, mode == 2 && tick == 0);
				mmDisplay.update(panel, true, now, mode == 2 && tick == 0);
				const auto dim = model == md::MachineModel::Machinedrum ? mdDisplay.pulse : mmDisplay.pulse;
				const auto expected = step % 4 < 2 ? 1u << page : 0;
				expect(dim == expected, "section flash was late or on the old section (tempo/mode boundary matrix)");
				const auto frame = Renderer::render(panel, model, model, true, false, 1, dim, mode != 0, enabled);
				for(unsigned label = 0; label < 4; ++label)
					expect(labelHasColor(frame, 228 + label * 40,
						label >= count || (expected & (1u << label)) ? 0x3186 : 0xffff),
						"first-step section colour did not reach the final LCD frame");
			}
		}
	}

	void checkSelectedDrumLabels()
	{
		md::FrontPanel panel;
		for(int track = 0; track < 16; ++track)
		{
			panel.setSelectedMachinedrumTrack(track);
			// Native selected lamp stays ON during hits: the independent hit
			// signal must still turn its LCD text red and then back to white.
			for(int bank = 0; bank < 2; ++bank) {
				panel.processByte(0x24 + bank);
				panel.processByte(track / 8 == bank ? ~(1u << (track % 8)) : 0xff);
			}
			const auto idle = Renderer::render(panel, md::MachineModel::Machinedrum,
				md::MachineModel::Machinedrum, true);
			const auto hit = Renderer::render(panel, md::MachineModel::Machinedrum,
				md::MachineModel::Machinedrum, true, false, -1, 0, false, -1, 1u << track);
			const auto after = Renderer::render(panel, md::MachineModel::Machinedrum,
				md::MachineModel::Machinedrum, true);
			const unsigned x = track < 8 ? 12 : 452;
			const unsigned y = 46 + (track % 8) * 24;
			unsigned white = 0;
			for(unsigned row = y; row < y+10; ++row)
				for(unsigned column = x; column < x+16; ++column)
					if(pixel(idle, column, row) == 0xffff) {
						++white;
						expect(pixel(hit, column, row) == 0xf986, "selected MD LCD drum did not flash red");
						expect(pixel(after, column, row) == 0xffff, "selected MD LCD drum did not return to white");
					}
			expect(white > 0, "selected MD LCD drum label was not white");
		}
		panel.reset();
		expect(panel.getSelectedMachinedrumTrack() == -1, "panel reset retained a selected drum");
	}

	void checkModeLabels()
	{
		md::FrontPanel mdPanel;
		const auto mdBlank = Renderer::render(mdPanel,
			md::MachineModel::Machinedrum, md::MachineModel::Machinedrum, false);
		mdPanel.processByte(0x23);
		mdPanel.processByte(0xfe); // CLASSIC
		const auto mdClassic = Renderer::render(mdPanel,
			md::MachineModel::Machinedrum, md::MachineModel::Machinedrum, false);
		const auto mdDirty = Renderer::dirtyBounds(mdBlank, mdClassic);
		expect(!mdDirty.empty() && mdDirty.x >= 400 && mdDirty.y < 32,
			"Machinedrum CLASSIC/EXTENDED state was not drawn at top right");

		md::FrontPanel mmPanel;
		const auto mmBlank = Renderer::render(mmPanel,
			md::MachineModel::Monomachine, md::MachineModel::Monomachine, false);
		mmPanel.processByte(0x27);
		mmPanel.processByte(0xfd); // AMP
		const auto mmAmp = Renderer::render(mmPanel,
			md::MachineModel::Monomachine, md::MachineModel::Monomachine, false);
		const auto mmDirty = Renderer::dirtyBounds(mmBlank, mmAmp);
		expect(!mmDirty.empty() && mmDirty.x >= 400 && mmDirty.y < 32,
			"Monomachine trigger mode was not drawn at top right");
	}

	void writePreview(const char* const _path)
	{
		const auto panel = diagnosticPanel();
		const auto left = Renderer::render(panel, md::MachineModel::Machinedrum,
			md::MachineModel::Machinedrum, true);
		const auto right = Renderer::render(panel, md::MachineModel::Monomachine,
			md::MachineModel::Machinedrum, true);
		std::ofstream output(_path, std::ios::binary);
		expect(output.good(), "could not create preview image");
		output << "P6\n" << Renderer::g_width * 2 << ' '
			<< Renderer::g_height << "\n255\n";
		const auto writeFrame = [&output](const Renderer::Frame& _frame,
			const unsigned _x, const unsigned _y)
		{
			const auto value = _frame[static_cast<size_t>(_y) * Renderer::g_width + _x];
			const uint8_t rgb[] =
			{
				static_cast<uint8_t>(((value >> 11) & 0x1f) * 255 / 31),
				static_cast<uint8_t>(((value >> 5) & 0x3f) * 255 / 63),
				static_cast<uint8_t>((value & 0x1f) * 255 / 31),
			};
			output.write(reinterpret_cast<const char*>(rgb), sizeof(rgb));
		};
		for(unsigned y = 0; y < Renderer::g_height; ++y)
		{
			for(unsigned x = 0; x < Renderer::g_width; ++x)
				writeFrame(left, x, y);
			for(unsigned x = 0; x < Renderer::g_width; ++x)
				writeFrame(right, x, y);
		}
	}
}

int main(const int _argc, const char* const* const _argv)
{
	checkLcdGeometryAndPalette();
	checkDirtyBounds();
	checkFocusAndTransportOverlays();
	checkPanelLedOverlays();
	checkPatternLengthSections();
	checkRecordingWithDifferentSelectedAndPlayingSections();
	checkLiveAndGridRecordingDisplay();
	checkReversedFlashPhaseOnBothDisplays();
	checkMonomachinePageFlashStartsAtFirstStep();
	checkModeLabels();
	checkSelectedDrumLabels();
	checkSectionBoundaryFrames();
	if(_argc == 3 && std::string(_argv[1]) == "--preview")
		writePreview(_argv[2]);
	std::cout << "MD/MM Maschine screen renderer: PASS\n";
	return 0;
}
