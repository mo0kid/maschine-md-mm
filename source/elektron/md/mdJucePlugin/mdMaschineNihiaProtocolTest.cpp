#include "mdMaschineNihiaProtocol.h"
#include "mdMaschineStripLeds.h"
#include "mdMaschineRecordedSteps.h"

#include <cstdlib>
#include <iostream>

namespace
{
	void expect(const bool _condition, const char* const _message)
	{
		if(_condition)
			return;
		std::cerr << "mdMaschineNihiaProtocolTest: " << _message << '\n';
		std::exit(1);
	}

	void checkStripLeds()
	{
		using namespace mdJucePlugin::maschine;
		for(const auto model : {md::MachineModel::Machinedrum, md::MachineModel::Monomachine})
		{
			md::FrontPanel panel;
			nihia::LedFrame frame;
			frame.fill(0x55);
			setStripLeds(frame, panel, model);
			for(size_t i = 0; i < frame.size(); ++i)
				expect(frame[i] == (i >= 62 && i < 87 ? 0 : 0x55),
					"strip reset must clear only strip LEDs, leaving pads/buttons alone");
			for(uint8_t step = 0; step < 16; ++step)
			{
				for(uint8_t state = 0; state < (model == md::MachineModel::Monomachine ? 4 : 2); ++state)
				{
					panel.reset();
					const bool mm = model == md::MachineModel::Monomachine;
					const uint8_t data[] = {static_cast<uint8_t>(0x20 + step / (mm ? 4 : 8)),
						static_cast<uint8_t>(0xff ^ (state << (mm ? 2 * (step % 4) : step % 8)))};
					panel.processBytes(data, sizeof(data));
					setStripLeds(frame, panel, model);
					const auto color = state == 0 ? nihia::LedColor::Off
						: !mm || state == 2 ? nihia::LedColor::Red
						: state == 1 ? nihia::LedColor::Green : nihia::LedColor::Yellow;
					for(uint8_t i = 0; i < 25; ++i)
						expect(frame[62 + i] == (i == step && state ? static_cast<uint8_t>(color) * 4 + 3 : 0),
							"native step lamp position/colour mismatch");
				}
			}
			for(uint8_t page = 0; page < 4; ++page)
			{
				panel.reset();
				const auto [bank, bit] = scalePageLed(model, page);
				const uint8_t data[] = {bank, static_cast<uint8_t>(0xff ^ (1u << bit))};
				panel.processBytes(data, sizeof(data));
				setStripLeds(frame, panel, model);
				for(uint8_t i = 0; i < 25; ++i)
					expect(frame[62 + i] == (i == 21 + page ? 7 : 0),
						"native section lamp position/colour mismatch");
				panel.reset();
				setStripLeds(frame, panel, model);
				expect(frame[83 + page] == 0, "section flashing must follow native lamp off phase");
			}
		}
	}

}

int main()
{
	checkStripLeds();
	{
		using namespace mdJucePlugin::maschine;
		using Color = md::FrontPanel::LedColor;
		using Pad = nihia::LedColor;
		expect(stepPadBrightness(Pad::Yellow) == 1, "occupied pads must be dark yellow");
		expect(stepPadBrightness(Pad::Red) == 2 && stepPadBrightness(Pad::White) == 3,
			"playhead must be brighter than occupied pads");
		md::FrontPanel mdPanel;
		mdPanel.processByte(0x20);
		mdPanel.processByte(0xfe); // Occupied first step.
		mdPanel.setMachinedrumPlaybackStep(2);
		expect(machinedrumPadColor(mdPanel, 0, true, true, true, 0) == Pad::Yellow,
			"MD occupied pad must stay yellow during recording");
		expect(machinedrumPadColor(mdPanel, 2, true, true, true, 0) == Pad::Red,
			"MD record cursor must be red even when native lamp is dark");
		for(unsigned step = 0; step < 16; ++step)
			expect(machinedrumPadColor(mdPanel, step, false, true, false, 0) == Pad::Off,
				"MD normal playback must not mirror native cursor/activity onto pads");
		expect(machinedrumPadColor(mdPanel, 2, true, false, true, 0) == Pad::Off,
			"MD stopped transport must not show a cursor");
		expect(machinedrumPadColor(mdPanel, 2, true, true, true, 1) == Pad::Off,
			"MD cursor must not leak into a different edit section");
		mdPanel.reset();
		expect(mdPanel.getMachinedrumPlaybackStep() == -1, "MD reset retained stale cursor");
		expect(monomachinePadColor(Color::Red, true, true) == Pad::Yellow,
			"MM grid trigs must be yellow, not native red");
		expect(monomachinePadColor(Color::Yellow, true, true) == Pad::Red,
			"MM grid playhead must be red");
		for(const bool record : {false, true}) {
			expect(monomachinePadColor(Color::Green, record, false) == Pad::Yellow,
				"MM playback/live trigs must stay yellow");
			for(const auto cursor : {Color::Red, Color::Yellow})
				expect(monomachinePadColor(cursor, record, false) == (record ? Pad::Red : Pad::White),
					"MM playback/live cursor colour incorrect");
			for(const bool grid : {false, true})
				expect(monomachinePadColor(Color::Off, record, grid) == Pad::Off,
					"MM empty step retained a playhead or old trig");
		}
	}
	using namespace mdJucePlugin::maschine;
	ScreenRenderer::Frame frame{};
	frame[0] = 0xf81f;
	frame[1] = 0x07e0;
	const auto message = nihia::encodeDisplayFrame(1, frame);
	expect(message.size() == 20 + 16 + 4 + frame.size() * 2 + 8,
		"encoded frame size is wrong");
	expect(message[0] == 0x44 && message[1] == 0x73
		&& message[2] == 0x64 && message[3] == 0x03,
		"outer display message id is wrong");
	expect(message[4] == 1 && message[20] == 0x84 && message[22] == 1,
		"right display id is not present in both headers");
	expect(message[40] == 0xf8 && message[41] == 0x1f
		&& message[42] == 0x07 && message[43] == 0xe0,
		"RGB565 pixels are not big endian");
	expect(message[message.size() - 8] == 0x03
		&& message[message.size() - 4] == 0x40,
		"display command trailer is wrong");

	nihia::LedFrame leds{};
	nihia::setLed(leds, 13, nihia::LedColor::Orange, 3);
	nihia::setLed(leds, 103, nihia::LedColor::Mint, 2);
	nihia::setLed(leds, 0, nihia::LedColor::White, 3);
	const auto ledMessage = nihia::encodeLedFrame(leds);
	expect(ledMessage.size() == 8 + nihia::g_ledCount,
		"encoded LED frame size is wrong");
	expect(ledMessage[0] == 0x00 && ledMessage[1] == 0x75
		&& ledMessage[2] == 0x6c && ledMessage[3] == 0x03,
		"LED message id is wrong");
	expect(ledMessage[4] == nihia::g_ledCount && ledMessage[20] == 11
		&& ledMessage.back() == 34,
		"LED count or palette encoding is wrong");

	const uint8_t button[] =
	{
		0x00, 0x4e, 0x73, 0x03, 0, 0, 0, 0, 0, 0, 0, 0,
		1, 0, 0, 0, 68, 0, 0, 0, 1, 0, 0, 0,
	};
	nihia::ButtonEvent event;
	expect(nihia::decodeButtonEvent(button, sizeof(button), event),
		"valid button event was rejected");
	expect(event.id == 68 && event.pressed,
		"button 5 event decoded incorrectly");
	expect(!nihia::decodeButtonEvent(button, 20, event),
		"truncated button event was accepted");

	const uint8_t knob[] =
	{
		0x00, 0x4e, 0x65, 0x03, 0, 0, 0, 0, 0, 0, 0, 0,
		1, 0, 0, 0, 7, 0, 0, 0, 0xff, 0xff, 0xff, 0xff,
	};
	nihia::KnobEvent knobEvent;
	expect(nihia::decodeKnobEvent(knob, sizeof(knob), knobEvent),
		"valid knob event was rejected");
	expect(knobEvent.index == 7 && knobEvent.rotation == -1,
		"knob event decoded incorrectly");

	const uint8_t mainKnob[] =
	{
		0x00, 0x4e, 0x77, 0x03, 0, 0, 0, 0, 0, 0, 0, 0,
		1, 0, 0, 0, 0xff, 0xff, 0xff, 0xff,
	};
	nihia::MainKnobEvent mainKnobEvent;
	expect(nihia::decodeMainKnobEvent(mainKnob, sizeof(mainKnob),
		mainKnobEvent) && mainKnobEvent.rotation == -1,
		"main knob event decoded incorrectly");

	const uint8_t pad[] =
	{
		0x00, 0x4e, 0x50, 0x03, 0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 12, 0, 0, 0, 1, 0, 0, 0, 100, 0, 0, 0,
	};
	nihia::PadEvent padEvent;
	expect(nihia::decodePadEvent(pad, sizeof(pad), padEvent),
		"valid pad event was rejected");
	expect(padEvent.index == 0 && padEvent.pressed && padEvent.pressure == 100,
		"pad event decoded incorrectly");

	std::cout << "Maschine NIHIA protocol: PASS\n";
	return 0;
}
