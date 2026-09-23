#pragma once

#include <memory>

namespace juce { class Image; class Graphics; class Colour; }
namespace juceRmlUi { class RmlComponent; class ElemCanvas; }

namespace mdJucePlugin
{
	// Single owner/gate for crisp panel rendering. The permanent
	// aspect-ratio and settings-resource identity fixes do not depend on it.
	class PixelPerfectPanel
	{
	public:
		static constexpr const char* configKey = "pixelPerfectPanel";
		static constexpr bool defaultEnabled = true;
		static constexpr const char* ruleClass = "elektronPixelRule";

		PixelPerfectPanel();
		~PixelPerfectPanel();
		void apply(juceRmlUi::RmlComponent& _component, juceRmlUi::ElemCanvas* _canvas, bool _enabled);
		// Returns false to use the editor's normal aspect-fit painter.
		bool paintLcd(const juce::Image& _lcd, juce::Graphics& _graphics, juce::Colour _background) const;
		bool isEnabled() const;

	private:
		struct Impl;
		std::unique_ptr<Impl> m_impl;
	};
}
