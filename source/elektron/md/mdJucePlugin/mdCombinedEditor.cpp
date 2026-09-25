#include "mdCombinedEditor.h"

#include "mdCombinedProcessor.h"
#include "mdEditor.h"
#include "mdLib/mdsysexfile.h"
#include "jucePluginEditorLib/pluginEditorWindow.h"
#include "jucePluginEditorLib/pluginEditorState.h"

#include <algorithm>

namespace mdJucePlugin
{
	namespace
	{
		enum SysexCommand
		{
			SendMd = 1, SendMm, ResumeMd, ResumeMm, CancelMd, CancelMm,
			SaveMd, SaveMm, FinishSave, CancelSave, ResetMd, ResetMm
		};
	}

	struct CombinedEditor::SysexMenu final : juce::MenuBarModel
	{
		explicit SysexMenu(CombinedEditor& _owner) : owner(_owner) {}

		juce::StringArray getMenuBarNames() override { return {"File"}; }

		juce::PopupMenu getMenuForIndex(int, const juce::String&) override
		{
			juce::PopupMenu menu;
			const bool saving = owner.m_processor.isSysexCapturing()
				|| owner.m_sysexSaveChooserOpen;
			const auto* mdEditor = owner.editorFor(false);
			const auto* mmEditor = owner.editorFor(true);
			const bool sending = (mdEditor && mdEditor->isUserSysexTransferActive())
				|| (mmEditor && mmEditor->isUserSysexTransferActive());
			const auto addMachine = [&](const bool mm, const int send,
				const int resume, const int cancel)
			{
				const auto* editor = owner.editorFor(mm);
				const juce::String name = mm ? "Monomachine" : "Machinedrum";
				const bool active = editor && editor->isUserSysexTransferActive();
				menu.addItem(send, "Load SysEx File to " + name + "...",
					editor && !active && !saving);
				if(editor && editor->canResumeUserSysexTransfer())
					menu.addItem(resume, "Resume " + name + " SysEx Transfer");
				if(editor && editor->canCancelUserSysexTransfer())
					menu.addItem(cancel, "Cancel " + name + " SysEx Transfer");
			};
			addMachine(false, SendMd, ResumeMd, CancelMd);
			addMachine(true, SendMm, ResumeMm, CancelMm);
			menu.addSeparator();
			if(owner.m_processor.isSysexCapturing())
			{
				menu.addItem(FinishSave, "Finish Saving SysEx Dump");
				menu.addItem(CancelSave, "Cancel Saving SysEx Dump");
			}
			else
			{
				menu.addItem(SaveMd, "Save Machinedrum SysEx Dump...",
					!sending && !owner.m_sysexSaveChooserOpen);
				menu.addItem(SaveMm, "Save Monomachine SysEx Dump...",
					!sending && !owner.m_sysexSaveChooserOpen);
			}
			menu.addSeparator();
			juce::PopupMenu reset;
			reset.addItem(ResetMd, "Machinedrum...");
			reset.addItem(ResetMm, "Monomachine...");
			menu.addSubMenu("Factory Reset", reset, !saving && !sending && !owner.m_factoryResetPending);
			return menu;
		}

		void menuItemSelected(const int _id, int) override
		{
			juce::Component::SafePointer<CombinedEditor> safe(&owner);
			juce::MessageManager::callAsync([safe, id = _id]
			{
				if(!safe) return;
				auto* const md = safe->editorFor(false);
				auto* const mm = safe->editorFor(true);
				switch(id)
				{
				case SendMd: if(md && !safe->m_processor.isSysexCapturing()) md->chooseUserSysexFile(); break;
				case SendMm: if(mm && !safe->m_processor.isSysexCapturing()) mm->chooseUserSysexFile(); break;
				case ResumeMd: if(md) md->resumeUserSysexTransfer(); break;
				case ResumeMm: if(mm) mm->resumeUserSysexTransfer(); break;
				case CancelMd: if(md) md->cancelUserSysexTransfer(); break;
				case CancelMm: if(mm) mm->cancelUserSysexTransfer(); break;
				case SaveMd: safe->startSysexSave(false); break;
				case SaveMm: safe->startSysexSave(true); break;
				case FinishSave: safe->finishSysexSave(); break;
				case CancelSave: safe->cancelSysexSave(); break;
				case ResetMd: safe->confirmFactoryReset(false); break;
				case ResetMm: safe->confirmFactoryReset(true); break;
				default: break;
				}
			});
		}

		CombinedEditor& owner;
	};

	CombinedEditor::CombinedEditor(CombinedProcessor& _processor)
		: AudioProcessorEditor(&_processor)
		, m_processor(_processor)
		, m_mdEditor(_processor.machinedrum().createEditorIfNeeded())
		, m_mmEditor(_processor.monomachine().createEditorIfNeeded())
	{
		for(auto* editor : {m_mdEditor.get(), m_mmEditor.get()})
		{
			if(auto* const window = dynamic_cast<jucePluginEditorLib::EditorWindow*>(editor))
				window->setEmbedded(true, 100.0f);
			if(editor)
			{
				addAndMakeVisible(editor);
				editor->addMouseListener(this, true);
			}
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
		#if JUCE_MAC
		if(juce::JUCEApplicationBase::isStandaloneApp())
		{
			m_sysexMenu = std::make_unique<SysexMenu>(*this);
			juce::MenuBarModel::setMacMainMenu(m_sysexMenu.get());
		}
		#endif

		// JUCE Standalone applies a 600x400 placeholder after createEditor returns.
		// Restore the composite once the native parent exists, just like the normal
		// single-product EditorWindow does for its configured scale.
		startTimer(50);
	}

	CombinedEditor::~CombinedEditor()
	{
		for(auto* editor : {m_mdEditor.get(), m_mmEditor.get()})
			if(editor) editor->removeMouseListener(this);
		#if JUCE_MAC
		if(juce::MenuBarModel::getMacMainMenu() == m_sysexMenu.get())
			juce::MenuBarModel::setMacMainMenu(nullptr);
		#endif
		cancelSysexSave();
		stopTimer();
		setConstrainer(nullptr);
	}

	Editor* CombinedEditor::editorFor(const bool _monomachine) const
	{
		auto& processor = _monomachine ? m_processor.monomachine()
			: m_processor.machinedrum();
		auto* const state = processor.getEditorState();
		return state ? dynamic_cast<Editor*>(state->getEditor()) : nullptr;
	}

	void CombinedEditor::confirmFactoryReset(const bool _monomachine)
	{
		if(m_factoryResetPending || m_processor.isSysexCapturing() || m_sysexSaveChooserOpen
			|| (editorFor(false) && editorFor(false)->isUserSysexTransferActive())
			|| (editorFor(true) && editorFor(true)->isUserSysexTransferActive())) return;
		m_factoryResetPending = true;
		const juce::String name = _monomachine ? "Monomachine" : "Machinedrum";
		juce::Component::SafePointer<CombinedEditor> safe(this);
		juce::AlertWindow::showOkCancelBox(juce::AlertWindow::WarningIcon,
			"Factory Reset " + name + "?",
			"Replace all current " + name + " patterns, kits, global settings and "
				"sample data with the factory defaults? Unsaved changes will be lost. "
				"Save any data you want to keep before continuing. The machine will restart.",
			"Factory Reset", "Cancel", this, juce::ModalCallbackFunction::create([safe, _monomachine](int result) {
				if(!safe) return;
				safe->m_factoryResetPending = false;
				if(result != 1) return;
				auto& processor = _monomachine ? safe->m_processor.monomachine() : safe->m_processor.machinedrum();
				juce::String error;
				if(!processor.factoryReset(error))
					juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
						"Factory Reset failed", error, "OK", safe.getComponent());
			}));
	}

	void CombinedEditor::startSysexSave(const bool _monomachine)
	{
		if(m_processor.isSysexCapturing() || m_sysexSaveChooserOpen
			|| (editorFor(false) && editorFor(false)->isUserSysexTransferActive())
			|| (editorFor(true) && editorFor(true)->isUserSysexTransferActive()))
			return;
		const auto name = _monomachine ? "Monomachine" : "Machinedrum";
		const auto initial = juce::File::getSpecialLocation(
			juce::File::userDocumentsDirectory).getChildFile(
				juce::String(name) + " dump.syx");
		m_sysexSaveChooser = std::make_unique<juce::FileChooser>(
			"Save " + juce::String(name) + " SysEx dump", initial, "*.syx", true);
		m_sysexSaveChooserOpen = true;
		juce::Component::SafePointer<CombinedEditor> safe(this);
		m_sysexSaveChooser->launchAsync(
			juce::FileBrowserComponent::saveMode
				| juce::FileBrowserComponent::canSelectFiles
				| juce::FileBrowserComponent::warnAboutOverwriting,
			[safe, model = _monomachine ? md::MachineModel::Monomachine
				: md::MachineModel::Machinedrum](const juce::FileChooser& _chooser)
			{
				if(!safe) return;
				auto file = _chooser.getResult();
				safe->m_sysexSaveChooserOpen = false;
				if(file == juce::File()) return;
				if(!file.hasFileExtension("syx"))
					file = file.withFileExtension("syx");
				if(!safe->m_processor.beginSysexCapture(model)) return;
				safe->m_sysexSaveFile = file;
				// macOS caches the native menu until the model announces a change.
				if(safe->m_sysexMenu) safe->m_sysexMenu->menuItemsChanged();
				juce::AlertWindow::showMessageBoxAsync(
					juce::AlertWindow::InfoIcon, "Saving SysEx dump",
					"On the emulated machine, open its SysEx SEND screen and send the "
					"dump you want to save. Then choose File > Finish Saving SysEx "
					"Dump from the macOS menu bar.", "OK", safe.getComponent());
			});
	}

	void CombinedEditor::finishSysexSave()
	{
		auto capture = m_processor.endSysexCapture();
		if(!capture) return;
		if(m_sysexMenu) m_sysexMenu->menuItemsChanged();
		const auto file = m_sysexSaveFile;
		m_sysexSaveFile = juce::File();
		juce::String error;
		if(capture->incomplete)
			error = "The dump was incomplete. No file was saved; please try again.";
		else if(capture->bytes.empty())
			error = "No SysEx dump was received. Use the emulated machine's SysEx SEND screen, then finish saving.";
		else if(const auto valid = md::validateMidiSysexStream(
			capture->bytes, capture->model);
			valid != md::MidiSysexStreamValidation::Valid)
			error = md::midiSysexValidationMessage(valid);
		else if(!file.replaceWithData(capture->bytes.data(),
			capture->bytes.size()))
			error = "The dump could not be written to the selected file.";
		juce::AlertWindow::showMessageBoxAsync(
			error.isEmpty() ? juce::AlertWindow::InfoIcon
				: juce::AlertWindow::WarningIcon,
			error.isEmpty() ? "SysEx dump saved" : "SysEx dump not saved",
			error.isEmpty() ? file.getFullPathName() : error, "OK", this);
	}

	void CombinedEditor::cancelSysexSave()
	{
		(void)m_processor.endSysexCapture();
		m_sysexSaveFile = juce::File();
		if(m_sysexMenu) m_sysexMenu->menuItemsChanged();
	}

	void CombinedEditor::mouseDown(const juce::MouseEvent& _event)
	{
		if(m_mmEditor && (_event.eventComponent == m_mmEditor.get()
			|| m_mmEditor->isParentOf(_event.eventComponent)))
			m_processor.setFocusedModel(md::MachineModel::Monomachine);
		else if(m_mdEditor && (_event.eventComponent == m_mdEditor.get()
			|| m_mdEditor->isParentOf(_event.eventComponent)))
			m_processor.setFocusedModel(md::MachineModel::Machinedrum);
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
