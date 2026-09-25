#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <memory>

namespace mdJucePlugin
{
	class CombinedProcessor;
	class Editor;

	class CombinedEditor final : public juce::AudioProcessorEditor,
		private juce::Timer
	{
	public:
		explicit CombinedEditor(CombinedProcessor& _processor);
		~CombinedEditor() override;

		void paint(juce::Graphics& _graphics) override;
		void resized() override;
		void mouseDown(const juce::MouseEvent& _event) override;

	private:
		struct SysexMenu;
		friend struct SysexMenu;
		Editor* editorFor(bool _monomachine) const;
		void confirmFactoryReset(bool _monomachine);
		void startSysexSave(bool _monomachine);
		void finishSysexSave();
		void cancelSysexSave();
		void timerCallback() override;
		void restorePreferredSize();
		void fixParentWindowSize() const;

		CombinedProcessor& m_processor;
		std::unique_ptr<SysexMenu> m_sysexMenu;
		std::unique_ptr<juce::FileChooser> m_sysexSaveChooser;
		bool m_sysexSaveChooserOpen = false;
		bool m_factoryResetPending = false;
		juce::File m_sysexSaveFile;
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
