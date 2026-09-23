#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <memory>

namespace mdJucePlugin
{
	class CombinedProcessor;

	class CombinedEditor final : public juce::AudioProcessorEditor,
		private juce::Timer
	{
	public:
		explicit CombinedEditor(CombinedProcessor& _processor);
		~CombinedEditor() override;

		void paint(juce::Graphics& _graphics) override;
		void resized() override;

	private:
		void timerCallback() override;
		void restorePreferredSize();
		void fixParentWindowSize() const;

		std::unique_ptr<juce::AudioProcessorEditor> m_mdEditor;
		std::unique_ptr<juce::AudioProcessorEditor> m_mmEditor;
		juce::ComponentBoundsConstrainer m_sizeConstrainer;
		int m_mdNaturalWidth = 1100;
		int m_mdNaturalHeight = 570;
		int m_mmNaturalWidth = 1100;
		int m_mmNaturalHeight = 570;
		int m_naturalWidth = 1100;
		int m_naturalHeight = 1140;
		int m_preferredWidth = 900;
		int m_preferredHeight = 930;
		int m_restoreAttempts = 0;

		JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CombinedEditor)
	};
}
