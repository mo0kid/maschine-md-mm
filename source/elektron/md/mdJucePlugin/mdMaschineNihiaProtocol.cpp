#include "mdMaschineNihiaProtocol.h"

namespace mdJucePlugin::maschine::nihia
{
	namespace
	{
		constexpr uint32_t g_displayMessage = 0x03647344;
		constexpr uint32_t g_buttonMessage = 0x03734e00;
		constexpr uint32_t g_knobMessage = 0x03654e00;
		constexpr uint32_t g_mainKnobMessage = 0x03774e00;
		constexpr uint32_t g_padMessage = 0x03504e00;
		constexpr uint32_t g_ledMessage = 0x036c7500;

		void appendU32(Bytes& _data, const uint32_t _value)
		{
			_data.push_back(static_cast<uint8_t>(_value));
			_data.push_back(static_cast<uint8_t>(_value >> 8u));
			_data.push_back(static_cast<uint8_t>(_value >> 16u));
			_data.push_back(static_cast<uint8_t>(_value >> 24u));
		}

		uint32_t readU32(const uint8_t* const _data)
		{
			return static_cast<uint32_t>(_data[0])
				| (static_cast<uint32_t>(_data[1]) << 8u)
				| (static_cast<uint32_t>(_data[2]) << 16u)
				| (static_cast<uint32_t>(_data[3]) << 24u);
		}
	}

	Bytes encodeLedFrame(const LedFrame& _frame)
	{
		Bytes result;
		result.reserve(8 + _frame.size());
		appendU32(result, g_ledMessage);
		appendU32(result, static_cast<uint32_t>(_frame.size()));
		result.insert(result.end(), _frame.begin(), _frame.end());
		return result;
	}

	void setLed(LedFrame& _frame, const uint8_t _oneBasedIndex,
		const LedColor _color, const uint8_t _intensity)
	{
		if(_oneBasedIndex == 0 || _oneBasedIndex > _frame.size())
			return;
		const auto intensity = static_cast<uint8_t>(
			_intensity > 3 ? 3 : _intensity);
		_frame[_oneBasedIndex - 1] = static_cast<uint8_t>(
			static_cast<uint8_t>(_color) * 4u + intensity);
	}

	Bytes encodeDisplayFrame(const uint8_t _display,
		const ScreenRenderer::Frame& _frame)
	{
		constexpr size_t commandHeaderSize = 16;
		constexpr size_t pixelCommandSize = 4;
		constexpr size_t commandTrailerSize = 8;
		constexpr size_t outerHeaderSize = 20;
		constexpr size_t pixelBytes = ScreenRenderer::g_width
			* ScreenRenderer::g_height * sizeof(uint16_t);
		constexpr size_t payloadSize = commandHeaderSize + pixelCommandSize
			+ pixelBytes + commandTrailerSize;

		Bytes result;
		result.reserve(outerHeaderSize + payloadSize);
		appendU32(result, g_displayMessage);
		appendU32(result, _display);
		appendU32(result, 0);
		appendU32(result, 0x01e00110);
		appendU32(result, static_cast<uint32_t>(payloadSize));

		const uint8_t commandHeader[] =
		{
			0x84, 0x00, _display, 0x60, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x01, 0xe0, 0x01, 0x10,
		};
		result.insert(result.end(), std::begin(commandHeader),
			std::end(commandHeader));

		// One transmit command describes pairs of RGB565 pixels. The count is
		// a big-endian 24-bit value; a complete 480x272 frame is 65,280 pairs.
		constexpr uint32_t pairCount = ScreenRenderer::g_width
			* ScreenRenderer::g_height / 2;
		result.push_back(static_cast<uint8_t>(pairCount >> 24u));
		result.push_back(static_cast<uint8_t>(pairCount >> 16u));
		result.push_back(static_cast<uint8_t>(pairCount >> 8u));
		result.push_back(static_cast<uint8_t>(pairCount));
		for(const auto pixel : _frame)
		{
			result.push_back(static_cast<uint8_t>(pixel >> 8u));
			result.push_back(static_cast<uint8_t>(pixel));
		}

		const uint8_t trailer[] =
		{
			0x03, 0x00, 0x00, 0x00,
			0x40, 0x00, 0x00, 0x00,
		};
		result.insert(result.end(), std::begin(trailer), std::end(trailer));
		return result;
	}

	bool decodeButtonEvent(const uint8_t* const _data, const size_t _size,
		ButtonEvent& _event)
	{
		if(!_data || _size < 21 || readU32(_data) != g_buttonMessage
			|| readU32(_data + 12) != 1)
			return false;
		_event.id = readU32(_data + 16);
		_event.pressed = _data[20] != 0;
		return true;
	}

	bool decodeKnobEvent(const uint8_t* const _data, const size_t _size,
		KnobEvent& _event)
	{
		if(!_data || _size < 24 || readU32(_data) != g_knobMessage)
			return false;
		const auto index = readU32(_data + 16);
		if(index >= 8)
			return false;
		_event.index = static_cast<uint8_t>(index);
		_event.rotation = static_cast<int32_t>(readU32(_data + 20));
		return _event.rotation != 0;
	}

	bool decodeMainKnobEvent(const uint8_t* const _data, const size_t _size,
		MainKnobEvent& _event)
	{
		if(!_data || _size < 20 || readU32(_data) != g_mainKnobMessage)
			return false;
		// NIHIA omits the knob index for the sole 4D/5D encoder. Some agent
		// revisions append a reserved index word, hence the two accepted sizes.
		const auto offset = _size >= 24 ? size_t{20} : size_t{16};
		_event.rotation = static_cast<int32_t>(readU32(_data + offset));
		return _event.rotation != 0;
	}

	bool decodePadEvent(const uint8_t* const _data, const size_t _size,
		PadEvent& _event)
	{
		if(!_data || _size < 28 || readU32(_data) != g_padMessage)
			return false;
		const auto code = readU32(_data + 16);
		if(code >= 16)
			return false;
		const auto row = code / 4;
		const auto column = code % 4;
		_event.index = static_cast<uint8_t>((3 - row) * 4 + column);
		_event.pressure = readU32(_data + 24);
		_event.pressed = _event.pressure != 0;
		return true;
	}
}
