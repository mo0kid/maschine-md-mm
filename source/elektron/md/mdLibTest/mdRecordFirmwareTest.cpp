#include "mdLib/mddevice.h"
#include "mdLib/mdromloader.h"
#include "mdLib/mdmidiprotocol.h"
#include "mdLib/mdsysexautomation.h"
#include "baseLib/filesystem.h"
#include "../mdJucePlugin/mdMaschineRecordedSteps.h"
#include "../mdJucePlugin/mdMaschineSectionPlayback.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string_view>

namespace
{
	void require(const bool _condition, const char* const _message)
	{
		if(!_condition)
			throw std::runtime_error(_message);
	}

	void advance(md::Hardware& _hardware, uint32_t _frames)
	{
		while(_frames)
		{
			const auto count = std::min(_frames, 256u);
			_hardware.advance(count);
			_frames -= count;
		}
	}

	bool recordLed(const md::Hardware& _hardware, const md::MachineModel _model)
	{
		const auto panel = _hardware.getFrontPanelSnapshot();
		return _model == md::MachineModel::Machinedrum
			? panel.getModeLed(md::FrontPanel::ModeLed::Record)
			: (panel.getLedBankRaw(0x27) & 1u) == 0;
	}
}

int main(const int _argc, const char* const* const _argv)
{
	if(_argc < 2)
		return 1;
	const auto model = std::string_view(_argv[1]) == "md"
		? md::MachineModel::Machinedrum : md::MachineModel::Monomachine;
	const char* const path = std::getenv(model == md::MachineModel::Machinedrum
		? "GEARMULATOR_MD_FIRMWARE_BIN" : "GEARMULATOR_MM_FIRMWARE_BIN");
	if(!path || !*path)
	{
		std::cout << "SKIP: firmware not supplied\n";
		return 77;
	}
	try
	{
		synthLib::DeviceCreateParams params;
		require(baseLib::filesystem::readFile(params.romData, path),
			"cannot read firmware");
		require(md::RomLoader::isRomForModel(params.romData, model),
			"wrong firmware model");
		params.romName = path;
		params.customData = md::deviceCustomData(model);
		// No homePath: never read or change the user's machine storage.
		auto device = std::make_unique<md::Device>(params);
		auto& hardware = device->getHardware();
		advance(hardware, md::g_samplerate * 25);
		require(hardware.isAudioReady() && hardware.isFirmwareMidiReady(),
			"boot incomplete");
		md::PanelRowState rows;
		if(_argc >= 3 && std::string_view(_argv[2]) == "--md-pad-cursor")
		{
			require(model == md::MachineModel::Machinedrum, "MD cursor fixture requires MD");
			using namespace mdJucePlugin::maschine;
			const unsigned length = _argc > 3 && std::string_view(_argv[3]) == "64" ? 64 : 32;
			const unsigned pages = (1u << (length / 16)) - 1;
			const auto tap = [&](md::PanelControl control) {
				const auto packet = md::panelPacket(model, control).value();
				for(const auto event : {rows.press(packet), rows.release(packet)}) {
					require(hardware.trySendPanelEvent(event.row, event.mask), "panel event rejected");
					advance(hardware, md::g_samplerate / 10);
				}
			};
			tap(md::PanelControl::Record);
			const auto function = md::panelPacket(model, md::PanelControl::Function).value();
			const auto down = rows.press(function);
			require(hardware.trySendPanelEvent(down.row, down.mask), "function rejected");
			advance(hardware, md::g_samplerate / 10);
			tap(md::PanelControl::Scale);
			const auto up = rows.release(function);
			require(hardware.trySendPanelEvent(up.row, up.mask), "function release rejected");
			advance(hardware, md::g_samplerate / 10);
			for(int attempt = 0; attempt < 4 && occupiedScalePages(hardware.getFrontPanelSnapshot(), model) != pages; ++attempt)
				tap(md::PanelControl::Scale);
			require(occupiedScalePages(hardware.getFrontPanelSnapshot(), model) == pages, "pattern length setup failed");
			tap(md::PanelControl::Enter);
			tap(md::PanelControl::Play);
			advance(hardware, md::g_samplerate / 10);
			for(unsigned step = 2; step < length * 2 + 2; ++step) {
				const auto panel = hardware.getFrontPanelSnapshot();
				require(panel.getMachinedrumPlaybackStep() == int(step % length), "MD cursor lost native sequencer position");
				for(unsigned pad = 0; pad < 16; ++pad) {
					for(unsigned selected = 0; selected < length / 16; ++selected) {
						const auto c = machinedrumPadColor(panel, pad, true, true, true, selected);
						require((c == nihia::LedColor::Red) == (pad == step % 16 && selected == (step % length) / 16),
							"MD red record cursor wrong or leaked into unselected section");
					}
					require(machinedrumPadColor(panel, pad, false, true, false, 0) == nihia::LedColor::Off,
						"MD normal playback must not show a pad cursor");
				}
				advance(hardware, md::g_samplerate * 120 / 1000);
			}
			std::cout << "PASS: MD native cursor and pad colours across " << length << "-step wraps\n";
			return 0;
		}
		if(_argc >= 3 && std::string_view(_argv[2]) == "--step-pages")
		{
			require(model == md::MachineModel::Monomachine, "step-page fixture requires MM");
			using namespace mdJucePlugin::maschine;
			using Native = md::FrontPanel::LedColor;
			using Pad = nihia::LedColor;
			const auto key = [&](md::PanelControl control, bool down) {
				const auto packet = md::panelPacket(model, control).value();
				const auto event = down ? rows.press(packet) : rows.release(packet);
				require(hardware.trySendPanelEvent(event.row, event.mask), "panel event rejected");
				advance(hardware, md::g_samplerate / 10);
			};
			const auto tap = [&](md::PanelControl control) { key(control, true); key(control, false); };
			tap(md::PanelControl::Record);
			key(md::PanelControl::Function, true);
			tap(md::PanelControl::Scale);
			key(md::PanelControl::Function, false);
			for(int n = 0; n < 4 && mdJucePlugin::maschine::occupiedScalePages(hardware.getFrontPanelSnapshot(), model) != 3; ++n)
				tap(md::PanelControl::Scale);
			tap(md::PanelControl::Enter);
			for(int i : {4, 8, 12})
				tap(static_cast<md::PanelControl>(static_cast<uint8_t>(md::PanelControl::Trigger1) + i));
			key(md::PanelControl::Play, true);
			bool sawOtherSection = false, sawCursor = false;
			for(int tick = 0; tick < 80; ++tick) {
				if(tick == 1) key(md::PanelControl::Play, false);
				const auto panel = hardware.getFrontPanelSnapshot();
				sawOtherSection |= (occupiedScalePages(panel, model) & 2) != 0;
				for(int i = 0; i < 16; ++i) {
					const auto c = panel.getMonomachineStepLedColor(i);
					const auto pad = monomachinePadColor(c, true, true);
					if(c == Native::Yellow) {
						sawCursor = true;
						require(pad == Pad::Red, "grid cursor not red");
					} else {
						require(pad == (i % 4 == 0 ? Pad::Yellow : Pad::Off),
							"grid pads flickered or left a trail across sections");
					}
				}
				advance(hardware, md::g_samplerate / 10);
			}
			require(sawOtherSection && sawCursor, "multi-section playback not exercised");
			std::cout << "PASS: MM yellow trigs/red cursor remain stable across two sections\n";
			return 0;
		}
		if(_argc >= 3 && std::string_view(_argv[2]) == "--step-colours")
		{
			require(model == md::MachineModel::Monomachine, "step-colour fixture requires MM");
			using Color = md::FrontPanel::LedColor;
			using mdJucePlugin::maschine::monomachinePadColor;
			using Pad = mdJucePlugin::maschine::nihia::LedColor;
			const auto tap = [&](md::PanelControl control) {
				const auto packet = md::panelPacket(model, control).value();
				for(const auto event : {rows.press(packet), rows.release(packet)}) {
					require(hardware.trySendPanelEvent(event.row, event.mask), "panel event rejected");
					advance(hardware, md::g_samplerate / 10);
				}
			};
			tap(md::PanelControl::Record);
			const auto baseline = hardware.getFrontPanelSnapshot();
			require(baseline.getMonomachineStepLedColor(0) == Color::Red,
				"factory grid trig is not red");
			require(baseline.getMonomachineStepLedColor(1) == Color::Off,
				"factory second step is not empty");
			const auto playPacket = md::panelPacket(model, md::PanelControl::Play).value();
			const auto playDown = rows.press(playPacket);
			require(hardware.trySendPanelEvent(playDown.row, playDown.mask), "play rejected");
			bool sawEmptyPlayhead = false, sawEmptyAfterPlayhead = false;
			for(int tick = 0; tick < 100; ++tick) {
				if(tick == 20) {
					const auto up = rows.release(playPacket);
					require(hardware.trySendPanelEvent(up.row, up.mask), "play release rejected");
				}
				advance(hardware, md::g_samplerate / 200);
				const auto panel = hardware.getFrontPanelSnapshot();
				const auto empty = panel.getMonomachineStepLedColor(1);
				sawEmptyPlayhead |= empty == Color::Yellow;
				sawEmptyAfterPlayhead |= sawEmptyPlayhead && empty == Color::Off;
				require(monomachinePadColor(empty, true, true) == (empty == Color::Yellow ? Pad::Red : Pad::Off),
					"MM playhead left a pad trail on an empty step");
				require(monomachinePadColor(panel.getMonomachineStepLedColor(0), true, true) != Pad::Off,
					"MM recorded step disappeared during playback");
			}
			require(sawEmptyPlayhead && sawEmptyAfterPlayhead, "empty-step playhead cycle was not observed");
			std::cout << "PASS: MM grid-record step colours and no empty-step playhead trail\n";
			return 0;
		}
		if(_argc >= 3 && std::string_view(_argv[2]) == "--drums")
		{
			using namespace md::automation::sysex;
			synthLib::SMidiEvent query(synthLib::MidiEventSource::Host);
			const auto request = globalRequest(model, 0);
			query.sysex.assign(request.begin(), request.end());
			require(hardware.sendMidi(query), "global request rejected");
			advance(hardware, md::g_samplerate);
			std::vector<synthLib::SMidiEvent> midi;
			hardware.readMidiOut(midi);
			std::optional<GlobalDump> global;
			for(const auto& event : midi)
				if(auto parsed = parseGlobalDump(model, event.sysex)) global = parsed;
			require(global.has_value(), "missing MD global key map");
			constexpr uint8_t notes[] = {36,38,40,41,43,45,47,48,50,52,53,55,57,59,60,62};
			const auto select = [&](uint8_t track) {
				synthLib::SMidiEvent event(synthLib::MidiEventSource::Host);
				const auto body = md::midiProtocol::selectTrack(track);
				event.sysex = {0xf0};
				event.sysex.insert(event.sysex.end(), body.begin(), body.end());
				event.sysex.push_back(0xf7);
				require(hardware.sendMidi(event), "track selection rejected");
				advance(hardware, md::g_samplerate / 10);
				require(hardware.getFrontPanelSnapshot().getSelectedMachinedrumTrack() == track,
					"LCD selection metadata disagrees with selected track");
			};
			for(uint8_t track = 0; track < 16; ++track) {
				if(global->drumNoteMap[notes[track]] != track)
					std::cerr << "MAP " << unsigned(notes[track]) << " -> " << unsigned(global->drumNoteMap[notes[track]]) << '\n';
				require(global->drumNoteMap[notes[track]] == track, "MD global key map decoded incorrectly");
				select(track);
			}
			const auto tap = [&](md::PanelControl control) {
				const auto packet = md::panelPacket(model, control).value();
				for(const auto event : {rows.press(packet), rows.release(packet)}) {
					require(hardware.trySendPanelEvent(event.row, event.mask), "panel event rejected");
					advance(hardware, md::g_samplerate / 10);
				}
			};
			for(const uint8_t track : {0, 8}) {
				select(track);
				tap(md::PanelControl::Play);
				for(unsigned mode = 0; mode < 2; ++mode) {
					if(mode) tap(md::PanelControl::Record);
					unsigned hits = 0;
					for(unsigned tick = 0; tick < 250; ++tick) {
						advance(hardware, md::g_samplerate / 100);
						midi.clear();
						hardware.readMidiOut(midi);
						for(const auto& event : midi)
							if((event.a & 0xf0) == 0x90 && event.c && event.b == notes[track]) ++hits;
						const auto panel = hardware.getFrontPanelSnapshot();
						require(panel.getSelectedMachinedrumTrack() == track, "drum hit changed LCD selection");
						require(panel.getDrumLed(track), "selected native lamp unexpectedly changed");
					}
					require(hits > 0, "selected drum hits were not separately observable");
				}
				tap(md::PanelControl::Stop);
				tap(md::PanelControl::Record);
			}
			std::cout << "PASS: MD LCD selection for all 16 drums and independent hits in playback/record\n";
			return 0;
		}
		const auto record = md::panelPacket(model, md::PanelControl::Record);
		require(record.has_value(), "missing Record mapping");
		const auto send = [&](const bool _pressed)
		{
			const auto packet = _pressed ? rows.press(*record) : rows.release(*record);
			require(hardware.trySendPanelEvent(packet.row, packet.mask),
				"Record packet rejected");
		};
		const auto before = recordLed(hardware, model);
		const auto holdMs = _argc >= 3 ? std::strtoul(_argv[2], nullptr, 10) : 100ul;
		send(true);
		advance(hardware, static_cast<uint32_t>(md::g_samplerate * holdMs / 1000));
		send(false);
		advance(hardware, md::g_samplerate / 2);
		const auto after = recordLed(hardware, model);
		std::cout << "Record hold " << holdMs << " ms: "
			<< before << " -> " << after << '\n';
		require(before != after, "Record did not toggle after the press");
		return 0;
	}
	catch(const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
