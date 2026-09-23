#pragma once

#include "mdLib/mdfrontpanel.h"
#include "mdLib/mdtypes.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace mdJucePlugin::maschine
{
	struct DirtyRect
	{
		uint16_t x = 0;
		uint16_t y = 0;
		uint16_t width = 0;
		uint16_t height = 0;

		bool empty() const { return width == 0 || height == 0; }
	};

	// Pure CPU renderer for one of the Maschine MK3's 480x272 RGB565 displays.
	// It deliberately consumes the reconstructed panel state instead of capturing
	// the plug-in UI, so display updates are independent of window visibility and
	// graphics-backend timing.
	class ScreenRenderer
	{
	public:
		static constexpr uint16_t g_width = 480;
		static constexpr uint16_t g_height = 272;
		static constexpr uint16_t g_lcdScale = 3;
		static constexpr uint16_t g_lcdX = 48;
		static constexpr uint16_t g_lcdY = 40;

		using Frame = std::array<uint16_t,
			static_cast<size_t>(g_width) * g_height>;

		static Frame render(const md::FrontPanel& _panel,
			md::MachineModel _displayedModel, md::MachineModel _controlledModel,
			bool _playing, bool _mixerHeld = false, int _selectedScalePage = -1,
			uint8_t _scalePulseMask = 0, bool _recordActive = false,
			int _enabledScalePages = -1, uint16_t _drumHitMask = 0);
		static void renderInto(Frame& _frame, const md::FrontPanel& _panel,
			md::MachineModel _displayedModel, md::MachineModel _controlledModel,
			bool _playing, bool _mixerHeld = false, int _selectedScalePage = -1,
			uint8_t _scalePulseMask = 0, bool _recordActive = false,
			int _enabledScalePages = -1, uint16_t _drumHitMask = 0);
		static DirtyRect dirtyBounds(const Frame& _before, const Frame& _after);

		static uint16_t lcdOnColor(md::MachineModel _model);
		static uint16_t lcdOffColor(md::MachineModel _model);

	private:
		static constexpr uint16_t rgb565(uint8_t _red, uint8_t _green, uint8_t _blue)
		{
			return static_cast<uint16_t>(((_red & 0xf8u) << 8u)
				| ((_green & 0xfcu) << 3u) | (_blue >> 3u));
		}

		static void fillRect(Frame& _frame, unsigned _x, unsigned _y,
			unsigned _width, unsigned _height, uint16_t _color);
		static void drawText(Frame& _frame, unsigned _x, unsigned _y,
			const char* _text, uint16_t _color, unsigned _scale = 2);
	};
}
