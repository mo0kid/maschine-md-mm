#pragma once

#include "mdMaschineScreenRenderer.h"

#include <array>
#include <cstdint>
#include <vector>

namespace mdJucePlugin::maschine::nihia
{
	// Wire-format implementation informed by the public Rebellion and cabl
	// Maschine MK3 protocol research. No Rebellion runtime is embedded here.
	struct ButtonEvent
	{
		uint32_t id = 0;
		bool pressed = false;
	};

	struct KnobEvent
	{
		uint8_t index = 0;
		int32_t rotation = 0;
	};

	struct MainKnobEvent
	{
		int32_t rotation = 0;
	};

	struct PadEvent
	{
		uint8_t index = 0;
		bool pressed = false;
		uint32_t pressure = 0;
	};

	using Bytes = std::vector<uint8_t>;
	static constexpr size_t g_ledCount = 103;
	using LedFrame = std::array<uint8_t, g_ledCount>;

	enum class LedColor : uint8_t
	{
		Off = 0,
		Red = 1,
		Orange = 2,
		Yellow = 5,
		Green = 7,
		Mint = 8,
		Cyan = 9,
		Blue = 11,
		Violet = 13,
		White = 17,
	};

	Bytes encodeDisplayFrame(uint8_t _display,
		const ScreenRenderer::Frame& _frame);
	Bytes encodeLedFrame(const LedFrame& _frame);
	void setLed(LedFrame& _frame, uint8_t _oneBasedIndex, LedColor _color,
		uint8_t _intensity);
	bool decodeButtonEvent(const uint8_t* _data, size_t _size,
		ButtonEvent& _event);
	bool decodeKnobEvent(const uint8_t* _data, size_t _size,
		KnobEvent& _event);
	bool decodeMainKnobEvent(const uint8_t* _data, size_t _size,
		MainKnobEvent& _event);
	bool decodePadEvent(const uint8_t* _data, size_t _size,
		PadEvent& _event);
}
