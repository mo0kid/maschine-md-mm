#include "mdCombinedMidiRouter.h"

#include <cstdlib>
#include <iostream>

namespace
{
	void expect(bool condition, const char* message)
	{
		if(condition) return;
		std::cerr << "mdCombinedMidiRouterTest: " << message << '\n';
		std::exit(1);
	}
}

int main()
{
	using Router = mdJucePlugin::CombinedMidiRouter;
	Router router;
	const auto md = md::MachineModel::Machinedrum;
	const auto mm = md::MachineModel::Monomachine;
	const auto route = [&](uint8_t status, uint8_t data1, uint8_t data2,
		md::MachineModel focus)
	{
		return router.route(status, data1, data2, focus);
	};
	expect(route(0x90, 60, 100, md) == Router::machinedrum,
		"MD did not receive focused note-on");
	expect(route(0x90, 61, 100, mm) == Router::monomachine,
		"MM did not receive focused note-on");
	expect(route(0x80, 60, 0, mm) == Router::machinedrum,
		"note-off missed original MD owner after focus switch");
	expect(route(0x90, 61, 0, md) == Router::monomachine,
		"zero-velocity note-off missed original MM owner");
	expect(route(0x90, 64, 100, md) == Router::machinedrum
		&& route(0x90, 64, 100, mm) == Router::monomachine
		&& route(0x80, 64, 0, mm) == Router::both,
		"overlapping same-pitch notes were left hanging");
	expect(route(0xb0, 64, 127, md) == Router::machinedrum
		&& route(0xb0, 64, 0, mm) == Router::machinedrum,
		"sustain release missed original destination");
	expect(route(0xb0, 1, 50, mm) == Router::monomachine
		&& route(0xc0, 12, 0, md) == Router::machinedrum
		&& route(0xe0, 0, 64, mm) == Router::monomachine,
		"performance controls ignored focus");
	expect(route(0xf8, 0, 0, md) == Router::both
		&& route(0xfa, 0, 0, mm) == Router::both
		&& route(0xfc, 0, 0, md) == Router::both
		&& route(0xf2, 0, 0, mm) == Router::both,
		"MIDI clock or transport stopped reaching both machines");
	expect(route(0xf0, 0x00, 0x20, mm) == Router::monomachine,
		"SysEx ignored focused destination");
	expect(route(0x91, 40, 100, md) == Router::machinedrum
		&& route(0xb1, 123, 0, mm) == Router::both
		&& route(0x81, 40, 0, mm) == Router::monomachine,
		"all-notes-off did not clear the correct channel owners");
	router.reset();
	expect(route(0x80, 64, 0, mm) == Router::monomachine,
		"reset retained stale note ownership");
	const auto mmChannels = [&](uint8_t status, uint8_t data1, uint8_t data2,
		uint8_t track, uint8_t base = 0)
	{
		return router.monomachineChannels(status, data1, data2, static_cast<uint8_t>(base + track));
	};
	expect(mmChannels(0x90, 60, 100, 2) == (1u << 2),
		"channel-1 keyboard note missed selected MM track 3");
	expect(mmChannels(0x80, 60, 0, 4) == (1u << 2),
		"MM note-off followed changed focus instead of original track");
	expect(mmChannels(0x91, 61, 100, 4) == (1u << 4),
		"MIDI channel 2 missed selected MM track 5");
	expect(mmChannels(0x90, 62, 100, 1, 3) == (1u << 4),
		"configured MM base channel was ignored");
	expect(mmChannels(0x90, 64, 100, 0) == 1
		&& mmChannels(0x90, 64, 100, 3) == (1u << 3)
		&& mmChannels(0x80, 64, 0, 2) == ((1u << 0) | (1u << 3)),
		"same note held across two MM tracks was not released on both");
	expect(mmChannels(0xb0, 64, 127, 1) == (1u << 1)
		&& mmChannels(0xb0, 64, 0, 4) == (1u << 1),
		"sustain-off missed original MM track");
	expect(mmChannels(0x90, 65, 100, 5) == (1u << 5)
		&& mmChannels(0xb0, 123, 0, 0) == ((1u << 4) | (1u << 5)),
		"all-notes-off missed occupied MM channels");
	router.reset();
	expect(mmChannels(0x80, 65, 0, 0) == 1,
		"MM channel owners survived reset");
	// Identical pitches on different input channels retain independent releases.
	expect(mmChannels(0x92, 60, 100, 4) == (1u << 4)
		&& mmChannels(0x9f, 60, 100, 1) == (1u << 1)
		&& mmChannels(0x82, 60, 0, 0) == (1u << 4)
		&& mmChannels(0x9f, 60, 0, 0) == (1u << 1),
		"cross-channel note release lost the original selected track");
	expect(mmChannels(0xb2, 64, 127, 4) == (1u << 4)
		&& mmChannels(0xbf, 64, 127, 1) == (1u << 1)
		&& mmChannels(0xb2, 64, 0, 0) == (1u << 4)
		&& mmChannels(0xbf, 64, 0, 0) == (1u << 1),
		"cross-channel sustain release lost the original selected track");

	expect(router.monomachineChannels(0x90, 70, 100, 0x7f) == 0,
		"unavailable note route fell back to a possible sequencer channel");
	expect(router.monomachineChannels(0x90, 71, 100, 4) == (1u << 4)
		&& router.monomachineChannels(0x80, 71, 0, 0x7f) == (1u << 4),
		"lost note-off when MIDI configuration became unavailable");

	std::cout << "Combined MIDI focus routing: PASS\n";
}
