#include "mdMaschineScreenRenderer.h"
#include "mdMaschineSectionPlayback.h"

#include <algorithm>
#include <array>
#include <limits>
#include <string>

namespace mdJucePlugin::maschine
{
	namespace
	{
		constexpr uint16_t g_background = 0x0841;
		constexpr uint16_t g_panel = 0x1082;
		constexpr uint16_t g_inactive = 0x3186;
		constexpr uint16_t g_text = 0xffff;
		constexpr uint16_t g_darkText = 0x0000;
		constexpr uint16_t g_redLed = 0xf986;
		constexpr uint16_t g_greenLed = 0x47e9;
		constexpr uint16_t g_yellowLed = 0xff0a;

		std::array<uint8_t, 5> glyph(const char _character)
		{
			switch(_character)
			{
			case '0': return {7, 5, 5, 5, 7};
			case '1': return {2, 6, 2, 2, 7};
			case '2': return {7, 1, 7, 4, 7};
			case '3': return {7, 1, 7, 1, 7};
			case '4': return {5, 5, 7, 1, 1};
			case '5': return {7, 4, 7, 1, 7};
			case '6': return {7, 4, 7, 5, 7};
			case '7': return {7, 1, 1, 1, 1};
			case '8': return {7, 5, 7, 5, 7};
			case '9': return {7, 5, 7, 1, 7};
			case 'A': return {2, 5, 7, 5, 5};
			case 'B': return {6, 5, 6, 5, 6};
			case 'C': return {3, 4, 4, 4, 3};
			case 'D': return {6, 5, 5, 5, 6};
			case 'E': return {7, 4, 6, 4, 7};
			case 'F': return {7, 4, 6, 4, 4};
			case 'G': return {3, 4, 5, 5, 3};
			case 'H': return {5, 5, 7, 5, 5};
			case 'I': return {7, 2, 2, 2, 7};
			case 'L': return {4, 4, 4, 4, 7};
			case 'M': return {5, 7, 7, 5, 5};
			case 'N': return {5, 7, 7, 7, 5};
			case 'O': return {2, 5, 5, 5, 2};
			case 'P': return {6, 5, 6, 4, 4};
			case 'R': return {6, 5, 6, 5, 5};
			case 'S': return {3, 4, 2, 1, 6};
			case 'T': return {7, 2, 2, 2, 2};
			case 'U': return {5, 5, 5, 5, 7};
			case 'V': return {5, 5, 5, 5, 2};
			case 'X': return {5, 5, 2, 5, 5};
			case 'Y': return {5, 5, 2, 2, 2};
			case '/': return {1, 1, 2, 4, 4};
			case ':': return {0, 2, 0, 2, 0};
			case '-': return {0, 0, 7, 0, 0};
			default: return {};
			}
		}

		const char* shortName(const md::MachineModel _model)
		{
			return _model == md::MachineModel::Monomachine ? "MM" : "MD";
		}

		const char* longName(const md::MachineModel _model)
		{
			return _model == md::MachineModel::Monomachine
				? "MONOMACHINE" : "MACHINEDRUM";
		}

		uint16_t accent(const md::MachineModel _model)
		{
			return _model == md::MachineModel::Monomachine
				? ScreenRenderer::lcdOffColor(_model) : 0xf9e7;
		}
	}

	uint16_t ScreenRenderer::lcdOnColor(const md::MachineModel _model)
	{
		return _model == md::MachineModel::Monomachine
			? rgb565(0x1a, 0x2b, 0x1e) : rgb565(0x38, 0x10, 0x0a);
	}

	uint16_t ScreenRenderer::lcdOffColor(const md::MachineModel _model)
	{
		return _model == md::MachineModel::Monomachine
			? rgb565(0xb9, 0xc8, 0xb2) : rgb565(0xe0, 0x47, 0x2b);
	}

	void ScreenRenderer::fillRect(Frame& _frame, const unsigned _x,
		const unsigned _y, const unsigned _width, const unsigned _height,
		const uint16_t _color)
	{
		const auto right = std::min<unsigned>(g_width, _x + _width);
		const auto bottom = std::min<unsigned>(g_height, _y + _height);
		for(auto y = _y; y < bottom; ++y)
			for(auto x = _x; x < right; ++x)
				_frame[static_cast<size_t>(y) * g_width + x] = _color;
	}

	void ScreenRenderer::drawText(Frame& _frame, const unsigned _x,
		const unsigned _y, const char* const _text, const uint16_t _color,
		const unsigned _scale)
	{
		if(!_text || _scale == 0)
			return;
		for(unsigned character = 0; _text[character] != '\0'; ++character)
		{
			const auto rows = glyph(_text[character]);
			const auto originX = _x + character * 4 * _scale;
			for(unsigned row = 0; row < rows.size(); ++row)
				for(unsigned column = 0; column < 3; ++column)
					if((rows[row] & (1u << (2u - column))) != 0)
						fillRect(_frame, originX + column * _scale,
							_y + row * _scale, _scale, _scale, _color);
		}
	}

	ScreenRenderer::Frame ScreenRenderer::render(const md::FrontPanel& _panel,
		const md::MachineModel _displayedModel,
		const md::MachineModel _controlledModel, const bool _playing,
		const bool _mixerHeld, const int _selectedScalePage,
		const uint8_t _scalePulseMask, const bool _recordActive, const int _enabledScalePages,
		const uint16_t _drumHitMask)
	{
		Frame frame;
		renderInto(frame, _panel, _displayedModel, _controlledModel, _playing,
			_mixerHeld, _selectedScalePage, _scalePulseMask, _recordActive, _enabledScalePages, _drumHitMask);
		return frame;
	}

	void ScreenRenderer::renderInto(Frame& frame, const md::FrontPanel& _panel,
		const md::MachineModel _displayedModel,
		const md::MachineModel _controlledModel, const bool _playing,
		const bool _mixerHeld, const int _selectedScalePage,
		const uint8_t _scalePulseMask, const bool /*_recordActive*/, const int _enabledScalePages,
		const uint16_t _drumHitMask)
	{
		frame.fill(g_background);

		const bool focused = _displayedModel == _controlledModel;
		const auto displayAccent = accent(_displayedModel);
		const auto focusColor = focused ? displayAccent : g_inactive;
		fillRect(frame, 0, 0, g_width, 32, g_panel);
		fillRect(frame, 4, 3, 112, 26, focusColor);
		drawText(frame, 12, 11, longName(_displayedModel),
			focused ? g_darkText : g_text, 2);

		const auto rawLed = [&_panel](const uint8_t _bank, const uint8_t _bit)
		{
			return ((_panel.getLedBankRaw(_bank) >> _bit) & 1u) == 0;
		};
		const auto occupiedPages = static_cast<uint8_t>(
			_enabledScalePages >= 0 ? _enabledScalePages
				: occupiedScalePages(_panel, _displayedModel) | _scalePulseMask);
		const auto drawPageLabels = [&frame, _selectedScalePage,
			_scalePulseMask, occupiedPages]()
		{
			for(uint8_t page = 0; page < 4; ++page)
			{
				// Centre the group at x=300: directly below the third button over
				// each of the Maschine displays (physical buttons 3 and 7).
				const auto x = static_cast<unsigned>(228 + page * 40);
				char label[] = {'1', ':', '4', '\0'};
				label[0] = static_cast<char>('1' + page);
				const bool playbackPulse = (_scalePulseMask & (1u << page)) != 0;
				const bool selected = static_cast<int>(page) == _selectedScalePage;
				const bool occupied = (occupiedPages & (1u << page)) != 0;
				const auto color = playbackPulse || !occupied ? g_inactive : g_text;
				drawText(frame, x, 11, label, color, 2);
				if(selected)
					fillRect(frame, x, 24, 22, 2, g_text);
			}
		};
		const auto drawEditPageLabel = [&frame, displayAccent](
			const char* const _label, const uint8_t _page,
			const uint8_t _pageCount)
		{
			if(!_label)
				return;
			// The current edit page belongs directly below the second button over
			// each display. Keep its centre at x=180, between the instrument name
			// below button 1 and the 1:4..4:4 group below button 3.
			const auto width = static_cast<unsigned>(
				std::char_traits<char>::length(_label)) * 8;
			const auto x = 180u > width / 2 ? 180u - width / 2 : 0u;
			drawText(frame, x, 11, _label, g_text, 2);

			constexpr unsigned dotSize = 3;
			constexpr unsigned dotSpacing = 8;
			const auto dotsWidth = static_cast<unsigned>(
				(_pageCount - 1) * dotSpacing + dotSize);
			const auto firstDotX = 180u - dotsWidth / 2;
			for(uint8_t dot = 0; dot < _pageCount; ++dot)
				fillRect(frame, firstDotX + dot * dotSpacing, 26,
					dotSize, dotSize, dot == _page ? displayAccent : g_inactive);
		};
		const auto drawModeLabel = [&frame](const char* const _label)
		{
			if(!_label || *_label == '\0')
				return;
			const auto width = static_cast<unsigned>(
				std::char_traits<char>::length(_label)) * 8;
			// Right-align inside the header, clear of the 4:4 page label.
			drawText(frame, 472u > width ? 472u - width : 0u, 11,
				_label, g_text, 2);
		};

		if(_displayedModel == md::MachineModel::Monomachine)
		{
			const bool trigAmp = rawLed(0x27, 1);
			const bool trigFilter = rawLed(0x27, 2);
			const bool trigLfo = rawLed(0x27, 3);
			drawModeLabel(trigAmp && trigFilter && trigLfo ? "ALL"
				: trigAmp ? "AMP" : trigFilter ? "FILTER"
				: trigLfo ? "LFO" : "");

			constexpr const char* editPages[] =
			{
				"SYNTHESIS", "AMP", "FILTER", "EFFECTS",
				"LFO 1", "LFO 2", "LFO 3",
			};
			constexpr uint8_t banks[] =
				{0x25, 0x25, 0x25, 0x25, 0x26, 0x26, 0x26};
			constexpr uint8_t bits[] = {4, 5, 6, 7, 0, 1, 2};
			for(uint8_t page = 0; page < std::size(editPages); ++page)
				if(rawLed(banks[page], bits[page]))
				{
					drawEditPageLabel(editPages[page], page,
						static_cast<uint8_t>(std::size(editPages)));
					break;
				}

			// The four 1/4..4/4 track-page lamps sit above the LCD on the
			// Monomachine. Track indicators 1..6 follow the right edge as on the panel.
			drawPageLabels();

			constexpr struct { uint8_t bank; uint8_t greenBit; } tracks[] =
			{
				{0x25, 0}, {0x25, 2}, {0x24, 0}, {0x24, 2}, {0x24, 4},
				{0x24, 6},
			};
			for(uint8_t track = 0; track < std::size(tracks); ++track)
			{
				const bool green = rawLed(tracks[track].bank,
					tracks[track].greenBit);
				const bool red = rawLed(tracks[track].bank,
					static_cast<uint8_t>(tracks[track].greenBit + 1));
				const auto color = green && red ? g_yellowLed
					: green ? g_greenLed : g_redLed;
				const auto y = static_cast<unsigned>(46 + track * 34);
				const std::string label = "T" + std::to_string(track + 1);
				drawText(frame, 452, y, label.c_str(),
					green || red ? color : g_inactive, 2);
			}
		}
		else
		{
			drawModeLabel(_panel.getModeLed(md::FrontPanel::ModeLed::Extended)
				? "EXTENDED"
				: _panel.getModeLed(md::FrontPanel::ModeLed::Classic)
					? "CLASSIC" : "");

			constexpr const char* editPages[] =
				{"SYNTHESIS", "EFFECTS", "ROUTING"};
			constexpr md::FrontPanel::StatusLed editPageLeds[] =
			{
				md::FrontPanel::StatusLed::Synthesis,
				md::FrontPanel::StatusLed::Effects,
				md::FrontPanel::StatusLed::Routing,
			};
			for(uint8_t page = 0; page < std::size(editPages); ++page)
				if(_panel.getStatusLed(editPageLeds[page]))
				{
					drawEditPageLabel(editPages[page], page,
						static_cast<uint8_t>(std::size(editPages)));
					break;
				}

			// MD page LEDs occupy the header. Its sixteen named sound-selection
			// indicators form two columns of eight around the LCD, matching the
			// two rows on the original panel.
			drawPageLabels();
			constexpr const char* drumNames[] =
			{
				"BD", "SD", "HT", "MT", "LT", "CP", "RS", "CB",
				"CH", "OH", "RC", "CC", "M1", "M2", "M3", "M4",
			};
			for(uint8_t drum = 0; drum < std::size(drumNames); ++drum)
			{
				const auto column = drum / 8;
				const auto row = drum % 8;
				const auto x = static_cast<unsigned>(column == 0 ? 12 : 452);
				const auto y = static_cast<unsigned>(46 + row * 24);
				const bool selected = drum == _panel.getSelectedMachinedrumTrack();
				const bool hit = (_drumHitMask & (1u << drum)) != 0;
				const auto color = hit ? g_redLed : selected ? g_text
					: _panel.getDrumLed(drum) ? displayAccent : g_inactive;
				drawText(frame, x, y, drumNames[drum], color, 2);
			}
		}

		const auto border = focused ? displayAccent : g_inactive;
		fillRect(frame, g_lcdX - 4, g_lcdY - 4,
			md::FrontPanel::g_lcdWidth * g_lcdScale + 8,
			md::FrontPanel::g_lcdHeight * g_lcdScale + 8, border);

		const auto on = lcdOnColor(_displayedModel);
		const auto off = lcdOffColor(_displayedModel);
		for(unsigned y = 0; y < md::FrontPanel::g_lcdHeight; ++y)
			for(unsigned x = 0; x < md::FrontPanel::g_lcdWidth; ++x)
				fillRect(frame, g_lcdX + x * g_lcdScale,
					g_lcdY + y * g_lcdScale, g_lcdScale, g_lcdScale,
					_panel.getLcdPixel(x, y) ? on : off);

		fillRect(frame, 0, 240, g_width, 32, g_panel);
		const unsigned firstEncoder = _displayedModel == md::MachineModel::Monomachine ? 4 : 0;
		for(unsigned i = 0; i < 4; ++i)
		{
			const auto x = i * 120;
			fillRect(frame, x + 2, 242, 116, 28,
				focused ? g_panel : g_background);
			if(_mixerHeld)
			{
				const auto* const label = firstEncoder == 0 && i == 0
					? "LEV" : firstEncoder == 4 && i == 3 ? "MASTER" : "-";
				const auto textWidth = static_cast<unsigned>(
					std::char_traits<char>::length(label)) * 8;
				drawText(frame, x + (120 - textWidth) / 2, 251, label,
					(firstEncoder == 0 && i == 0) || (firstEncoder == 4 && i == 3)
						? accent(_controlledModel) : g_inactive, 2);
			}
			else
			{
				char label[] = {'M', 'D', ' ', 'A', '\0'};
				label[1] = shortName(_controlledModel)[1];
				label[3] = static_cast<char>('A' + firstEncoder + i);
				drawText(frame, x + 40, 251, label,
					accent(_controlledModel), 2);
			}
		}
	}

	DirtyRect ScreenRenderer::dirtyBounds(const Frame& _before,
		const Frame& _after)
	{
		unsigned left = g_width;
		unsigned top = g_height;
		unsigned right = 0;
		unsigned bottom = 0;
		bool changed = false;
		for(unsigned y = 0; y < g_height; ++y)
			for(unsigned x = 0; x < g_width; ++x)
			{
				const auto index = static_cast<size_t>(y) * g_width + x;
				if(_before[index] == _after[index])
					continue;
				changed = true;
				left = std::min(left, x);
				top = std::min(top, y);
				right = std::max(right, x);
				bottom = std::max(bottom, y);
			}
		if(!changed)
			return {};
		return {static_cast<uint16_t>(left), static_cast<uint16_t>(top),
			static_cast<uint16_t>(right - left + 1),
			static_cast<uint16_t>(bottom - top + 1)};
	}
}
