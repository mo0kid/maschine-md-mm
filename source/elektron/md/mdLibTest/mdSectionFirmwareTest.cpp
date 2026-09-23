#include "mdLib/mddevice.h"
#include "baseLib/filesystem.h"
#include "../mdJucePlugin/mdMaschineSectionPlayback.h"
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string_view>

namespace {
void require(bool condition, const char* message)
{
	if(!condition) throw std::runtime_error(message);
}
}

int main(int argc, const char* const* argv)
{
	if(argc < 2) return 1;
	const auto model = std::string_view(argv[1]) == "md"
		? md::MachineModel::Machinedrum : md::MachineModel::Monomachine;
	const unsigned sectionCount = argc >= 3 && std::string_view(argv[2]) == "4" ? 4 : 2;
	const auto enabledMask = static_cast<uint8_t>((1u << sectionCount) - 1u);
	const auto* path = std::getenv(model == md::MachineModel::Machinedrum
		? "GEARMULATOR_MD_FIRMWARE_BIN" : "GEARMULATOR_MM_FIRMWARE_BIN");
	if(!path) return 77;
	try {
		synthLib::DeviceCreateParams params;
		require(baseLib::filesystem::readFile(params.romData, path), "cannot read firmware");
		params.romName = path;
		params.customData = md::deviceCustomData(model);
		// Isolated machine: no user storage is loaded or changed.
		auto device = std::make_unique<md::Device>(params);
		auto& hw = device->getHardware();
		const auto advance = [&](uint32_t frames) {
			while(frames) { const auto n = std::min(frames, 256u); hw.advance(n); frames -= n; }
		};
		md::PanelRowState rows;
		const auto key = [&](md::PanelControl control, bool down) {
			const auto packet = md::panelPacket(model, control).value();
			const auto event = down ? rows.press(packet) : rows.release(packet);
			require(hw.trySendPanelEvent(event.row, event.mask), "panel input rejected");
			advance(md::g_samplerate / 10);
		};
		const auto tap = [&](md::PanelControl control) { key(control, true); key(control, false); };
		using namespace mdJucePlugin::maschine;
		SectionDisplayState state;
		const auto sample = [&](bool record, bool play) {
			const auto panel = hw.getFrontPanelSnapshot();
			state.update(occupiedScalePages(panel, model), record, play, scaleSetupVisible(panel, model));
		};
		advance(md::g_samplerate * 28);
		sample(false, false);
		const auto initialLength = state.enabled;
		require(initialLength != 0, "missing initial pattern length");
		tap(md::PanelControl::Record);
		sample(true, false);
		require(state.enabled == initialLength, "entering record lost enabled sections");
		require(state.selected == 0, "initial edit section not first");
		key(md::PanelControl::Function, true);
		tap(md::PanelControl::Scale);
		key(md::PanelControl::Function, false);
		require(scaleSetupVisible(hw.getFrontPanelSnapshot(), model), "scale setup heading not detected");
		sample(true, false);
		for(unsigned attempt = 0; state.enabled != enabledMask && attempt < 4; ++attempt) {
			tap(md::PanelControl::Scale);
			sample(true, false);
		}
		require(state.enabled == enabledMask, "failed to set pattern length");
		tap(md::PanelControl::Enter);
		sample(true, false);
		require(state.enabled == enabledMask && state.selected == 0, "leaving scale setup lost section state");
		// Exercise the same final display state used by the Maschine worker.
		// These isolated factory fixtures run MD at 125 BPM and MM at 120 BPM.
		// Check inside the FIRST step of every section, not merely that each
		// page eventually flashes later in its bar.
		for(unsigned mode = 0; mode < 3; ++mode) {
			tap(md::PanelControl::Record); // Leave stopped grid mode.
			MachinedrumSectionDisplay mdDisplay;
			MonomachineSectionDisplay mmDisplay;
			const auto start = SectionFlashPhase::Clock::time_point{};
			mdDisplay.update(hw.getFrontPanelSnapshot(), false, start);
			mmDisplay.update(hw.getFrontPanelSnapshot(), false, start);
			if(mode == 1) {
				tap(md::PanelControl::Record);
				mdDisplay.update(hw.getFrontPanelSnapshot(), false, start);
				mmDisplay.update(hw.getFrontPanelSnapshot(), false, start);
			}
			const auto record = md::panelPacket(model, md::PanelControl::Record).value();
			const auto play = md::panelPacket(model, md::PanelControl::Play).value();
			if(mode == 2) {
				const auto event = rows.press(record);
				require(hw.trySendPanelEvent(event.row, event.mask), "REC chord rejected");
			}
			const auto event = rows.press(play);
			require(hw.trySendPanelEvent(event.row, event.mask), "PLAY rejected");
			const unsigned sectionTicks = model == md::MachineModel::Machinedrum ? 192 : 200;
			unsigned boundaries = 0;
			int previousLamps = -1;
			for(unsigned tick = 0; tick < sectionCount * sectionTicks * 2; ++tick) {
				advance(md::g_samplerate / 100);
				if(tick == 10) {
					for(const auto packet : {rows.release(record), rows.release(play)})
						require(hw.trySendPanelEvent(packet.row, packet.mask), "transport release rejected");
				}
				if(tick % 3 != 0) continue; // 30 ms display snapshots.
				const auto panel = hw.getFrontPanelSnapshot();
				const auto now = start + std::chrono::milliseconds((tick + 1) * 10);
				mdDisplay.update(panel, true, now, mode == 2 && tick == 0);
				mmDisplay.update(panel, true, now, mode == 2 && tick == 0);
				const auto lamps = occupiedScalePages(panel, model);
				if(std::getenv("GEARMULATOR_TRACE_SECTIONS") && mode == 1 && lamps != previousLamps) {
					std::cerr << "TRACE " << tick << " lamps=" << unsigned(lamps)
						<< " native=" << unsigned(model == md::MachineModel::Machinedrum ? mdDisplay.sections.pulse : mmDisplay.sections.pulse)
						<< " dim=" << unsigned(model == md::MachineModel::Machinedrum ? mdDisplay.pulse : mmDisplay.pulse)
						<< " realtime=" << (model == md::MachineModel::Machinedrum ? mdDisplay.recording.realtime : mmDisplay.recording.realtime) << '\n';
				}
				previousLamps = lamps;
				if(tick >= sectionTicks && tick % sectionTicks >= 5 && tick % sectionTicks <= 7) {
					const auto expected = 1u << ((tick / sectionTicks) % sectionCount);
					const auto dim = model == md::MachineModel::Machinedrum ? mdDisplay.pulse : mmDisplay.pulse;
					if(dim != expected) std::cerr << "Boundary mode=" << mode << " tick=" << tick << " expected=" << expected << " actual=" << unsigned(dim) << '\n';
					require(dim == expected, "final LCD indicator was late at the first step of the section");
					++boundaries;
				}
			}
			require(boundaries >= sectionCount, "not enough first-step boundaries tested");
			tap(md::PanelControl::Stop);
			if(mode == 0 || mode == 2) tap(md::PanelControl::Record); // Restore stopped grid.
		}
		std::cout << "PASS: first-step LCD boundary timing in playback, grid and live record\n";
		if(model == md::MachineModel::Monomachine) {
			tap(md::PanelControl::Trigger3);
			tap(md::PanelControl::Trigger7);
			tap(md::PanelControl::Trigger11);
			require(hw.getFrontPanelSnapshot().getMonomachineStepLedColor(2)
				!= md::FrontPanel::LedColor::Off, "populated-pattern fixture has no trig");
		}
		tap(md::PanelControl::Scale);
		sample(true, false);
		require(state.enabled == enabledMask && state.selected == 1, "second section not selected");
		if(model == md::MachineModel::Monomachine) {
			tap(md::PanelControl::Trigger3);
			tap(md::PanelControl::Trigger7);
			tap(md::PanelControl::Trigger11);
		}
		tap(md::PanelControl::Play);
		for(int focus : {1, 0, 1}) {
			for(unsigned attempt = 0; attempt < sectionCount && state.selected != focus; ++attempt) {
				const auto previous = state.selected;
				tap(md::PanelControl::Scale);
				// MM may acknowledge the next page during the lamp's dark phase.
				for(unsigned tick = 0; tick < 100 && state.selected == previous; ++tick) {
					advance(md::g_samplerate / 100);
					sample(true, true);
				}
			}
			require(state.selected == focus, "firmware did not report changed focus");
			uint8_t seenPulses = 0;
			for(unsigned tick = 0; tick < sectionCount * 200 + 100; ++tick) {
				advance(md::g_samplerate / 100);
				sample(true, true);
				require(state.selected == focus, "playback moved focus/underline");
				require(state.enabled == enabledMask, "playback or focus change lost enabled section");
				require((state.pulse & (state.pulse - 1)) == 0, "invalid playback pulse");
				seenPulses |= state.pulse;
			}
			require(seenPulses == enabledMask, "playing sections did not all flash");
		}
		tap(md::PanelControl::Stop);
		sample(true, false);
		require(state.pulse == 0 && state.enabled == enabledMask, "stopping left grey enabled section");
		if(model == md::MachineModel::Monomachine) {
			// A stopped grid-record page stays steady even without a transport
			// flag; removing that flag must not invent a playback flash.
			for(unsigned tick = 0; tick < 200; ++tick) {
				advance(md::g_samplerate / 100);
				const auto panel = hw.getFrontPanelSnapshot();
				const auto lamps = occupiedScalePages(panel, model);
				state.update(lamps, true, false, false);
				require(state.pulse == 0, "stopped MM record page flashed");
			}
		}
		tap(md::PanelControl::Record);
		sample(false, false);
		require(state.enabled == enabledMask, "leaving record lost pattern length");
		if(model == md::MachineModel::Monomachine) {
			// Grid recording entered during playback, with a desktop PLAY that
			// leaves the Maschine-local transport flag false.
			tap(md::PanelControl::Play);
			advance(md::g_samplerate / 2);
			tap(md::PanelControl::Record);
			uint8_t seenPulses = 0;
			unsigned lastRecordLitTick = 0;
			for(unsigned tick = 0; tick < sectionCount * 200 + 100; ++tick) {
				advance(md::g_samplerate / 100);
				const auto panel = hw.getFrontPanelSnapshot();
				const auto lamps = occupiedScalePages(panel, model);
				if((panel.getLedBankRaw(0x27) & 1u) == 0)
					lastRecordLitTick = tick;
				const bool record = tick - lastRecordLitTick < 120;
				state.update(lamps, record, false, false);
				seenPulses |= state.pulse;
			}
			require(seenPulses == enabledMask,
				"MM grid record did not flash all playing sections without Maschine PLAY");
			tap(md::PanelControl::Stop);
			// Return to normal mode before testing the simultaneous REC+PLAY
			// gesture used on Maschine, rather than PLAY then REC.
			tap(md::PanelControl::Record);
			advance(md::g_samplerate / 2);
			require((hw.getFrontPanelSnapshot().getLedBankRaw(0x27) & 1u) != 0,
				"MM failed to leave realtime record before chord test");
			MonomachineSectionDisplay hardwareDisplay;
			MonomachineSectionDisplay desktopDisplay;
			const auto start = SectionRecordingState::Clock::time_point{};
			hardwareDisplay.update(hw.getFrontPanelSnapshot(), false, start);
			desktopDisplay.update(hw.getFrontPanelSnapshot(), false, start);
			const auto record = md::panelPacket(model, md::PanelControl::Record).value();
			const auto play = md::panelPacket(model, md::PanelControl::Play).value();
			for(const auto packet : {rows.press(record), rows.press(play)})
				require(hw.trySendPanelEvent(packet.row, packet.mask),
					"simultaneous REC+PLAY press rejected");
			advance(md::g_samplerate / 10);
			for(const auto packet : {rows.release(record), rows.release(play)})
				require(hw.trySendPanelEvent(packet.row, packet.mask),
					"simultaneous REC+PLAY release rejected");
			advance(md::g_samplerate / 10);
			seenPulses = 0;
			int previousStep = -1;
			unsigned sectionWraps = 0;
			uint8_t seenDisplayedPulses = 0;
			for(unsigned tick = 0; tick < sectionCount * 200; ++tick) {
				advance(md::g_samplerate / 100);
				if(tick % 3 != 0) continue; // Match the hardware worker's ~30ms snapshots.
				const auto panel = hw.getFrontPanelSnapshot();
				const auto now = start + std::chrono::milliseconds(210 + tick * 10);
				hardwareDisplay.update(panel, true, now, tick == 0);
				desktopDisplay.update(panel, false, now);
				const auto step = monomachinePlayheadStep(panel);
				if(previousStep >= 14 && step == 0) {
					++sectionWraps;
					const auto expected = 1u << (sectionWraps % sectionCount);
					require(hardwareDisplay.pulse == expected && desktopDisplay.pulse == expected,
						"MM section LCD did not flash on the first step of the next page");
				}
				previousStep = step;
				seenPulses |= hardwareDisplay.sections.pulse;
				seenDisplayedPulses |= hardwareDisplay.pulse;
				require(hardwareDisplay.sections.enabled == enabledMask,
					"live recording lost enabled pages");
				if(tick > 100)
					require(hardwareDisplay.pulse == desktopDisplay.pulse,
						"desktop and Maschine live-record displays disagree");
			}
			require(seenPulses == enabledMask && seenDisplayedPulses == enabledMask,
				"MM REC+PLAY chord did not pulse every displayed section");
			require(sectionWraps == sectionCount,
				"MM playhead did not complete every section boundary");
			tap(md::PanelControl::Stop);
		}
		std::cout << "PASS: " << sectionCount << "-section focus switches and live-record display pulses\n";
		return 0;
	} catch(const std::exception& error) {
		std::cerr << "FAIL: " << error.what() << '\n';
		return 1;
	}
}
