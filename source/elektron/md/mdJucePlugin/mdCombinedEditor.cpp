#include "mdCombinedEditor.h"

#include "mdCombinedProcessor.h"
#include "jucePluginEditorLib/pluginEditorWindow.h"

#include <algorithm>

namespace mdJucePlugin
{
	CombinedEditor::CombinedEditor(CombinedProcessor& _processor)
		: AudioProcessorEditor(&_processor)
		, m_mdEditor(_processor.machinedrum().createEditorIfNeeded())
		, m_mmEditor(_processor.monomachine().createEditorIfNeeded())
	{
		for(auto* editor : {m_mdEditor.get(), m_mmEditor.get()})
		{
			if(auto* const window = dynamic_cast<jucePluginEditorLib::EditorWindow*>(editor))
				window->setEmbedded(true, 100.0f);
			if(editor)
				addAndMakeVisible(editor);
		}

		const auto mdWidth = m_mdEditor ? m_mdEditor->getWidth() : 1200;
		const auto mmWidth = m_mmEditor ? m_mmEditor->getWidth() : 1200;
		const auto mdHeight = m_mdEditor ? m_mdEditor->getHeight() : 500;
		const auto mmHeight = m_mmEditor ? m_mmEditor->getHeight() : 500;
		m_mdNaturalWidth = std::max(1, mdWidth);
		m_mdNaturalHeight = std::max(1, mdHeight);
		m_mmNaturalWidth = std::max(1, mmWidth);
		m_mmNaturalHeight = std::max(1, mmHeight);
		m_naturalWidth = std::max(m_mdNaturalWidth, m_mmNaturalWidth);
		m_naturalHeight = m_mdNaturalHeight + m_mmNaturalHeight;
		m_preferredWidth = m_naturalWidth;
		m_preferredHeight = m_naturalHeight;
		if(const auto* display = juce::Desktop::getInstance().getDisplays()
			.getPrimaryDisplay())
		{
			const auto maximum = display->userArea.reduced(24, 48);
			const auto scale = std::min(1.0,
				std::min(static_cast<double>(maximum.getWidth()) / m_naturalWidth,
					static_cast<double>(maximum.getHeight()) / m_naturalHeight));
			m_preferredWidth = std::max(600,
				static_cast<int>(static_cast<double>(m_naturalWidth) * scale));
			m_preferredHeight = std::max(600,
				static_cast<int>(static_cast<double>(m_naturalHeight) * scale));
		}
		m_sizeConstrainer.setMinimumSize(600,
			std::max(600, 600 * m_naturalHeight / m_naturalWidth));
		m_sizeConstrainer.setMaximumSize(3840,
			3840 * m_naturalHeight / m_naturalWidth);
		m_sizeConstrainer.setFixedAspectRatio(
			static_cast<double>(m_naturalWidth) / m_naturalHeight);
		setResizable(true, true);
		setConstrainer(&m_sizeConstrainer);
		restorePreferredSize();

		// JUCE Standalone applies a 600x400 placeholder after createEditor returns.
		// Restore the composite once the native parent exists, just like the normal
		// single-product EditorWindow does for its configured scale.
		startTimer(50);
	}

	CombinedEditor::~CombinedEditor()
	{
		stopTimer();
		setConstrainer(nullptr);
	}

	void CombinedEditor::paint(juce::Graphics& _graphics)
	{
		_graphics.fillAll(juce::Colours::black);
	}

	void CombinedEditor::resized()
	{
		const auto scale = std::min(
			static_cast<double>(getWidth()) / m_naturalWidth,
			static_cast<double>(getHeight()) / m_naturalHeight);
		const auto mdWidth = std::max(1,
			static_cast<int>(m_mdNaturalWidth * scale));
		const auto mdHeight = std::max(1,
			static_cast<int>(m_mdNaturalHeight * scale));
		const auto mmWidth = std::max(1,
			static_cast<int>(m_mmNaturalWidth * scale));
		const auto mmHeight = std::max(1,
			static_cast<int>(m_mmNaturalHeight * scale));
		const auto contentHeight = mdHeight + mmHeight;
		const auto top = (getHeight() - contentHeight) / 2;
		if(m_mdEditor)
			m_mdEditor->setBounds((getWidth() - mdWidth) / 2, top,
				mdWidth, mdHeight);
		if(m_mmEditor)
			m_mmEditor->setBounds((getWidth() - mmWidth) / 2, top + mdHeight,
				mmWidth, mmHeight);
	}

	void CombinedEditor::timerCallback()
	{
		restorePreferredSize();
		fixParentWindowSize();
		if(m_restoreAttempts == 0)
		{
			if(auto* const topLevel = getTopLevelComponent(); topLevel != this)
				topLevel->centreWithSize(topLevel->getWidth(), topLevel->getHeight());
		}
		if(++m_restoreAttempts >= 3)
			stopTimer();
	}

	void CombinedEditor::restorePreferredSize()
	{
		setSize(m_preferredWidth, m_preferredHeight);
	}

	void CombinedEditor::fixParentWindowSize() const
	{
		auto* parent = getParentComponent();
		while(parent)
		{
			if(parent->getWidth() < getWidth() || parent->getHeight() < getHeight())
				parent->setSize(std::max(parent->getWidth(), getWidth()),
					std::max(parent->getHeight(), getHeight()));
			parent = parent->getParentComponent();
		}
	}
}
