#pragma once

#include <array>
#include <deque>
#include <initializer_list>
#include <memory>
#include <optional>
#include <vector>

#include "jucePluginEditorLib/pluginEditor.h"

#include "mdFrontPanelPresentation.h"
#include "mdLcdGesture.h"
#include "mdLcdInteractionModel.h"
#include "mdPanelAffordances.h"
#include "mdLib/mdfrontpanel.h"
#include "mdLib/mdsyseximport.h"

#include "juce_gui_basics/juce_gui_basics.h"

namespace juce
{
	class Image;
	class Graphics;
}

namespace Rml
{
	class Element;
	class Event;
}

namespace juceRmlUi
{
	class ElemButton;
	class ElemCanvas;
	class ElemKnob;
}

namespace md
{
	class Hardware;
}

namespace mdJucePlugin
{
	class Controller;
	class PixelPerfectPanel;
	struct EditorIdentityTestAccess;

	class Editor final : public jucePluginEditorLib::Editor, juce::MultiTimer,
		private juce::FocusChangeListener
	{
	public:
		Editor(jucePluginEditorLib::Processor& _processor, const jucePluginEditorLib::Skin& _skin);
		~Editor() override;

		Editor(Editor&&) = delete;
		Editor(const Editor&) = delete;
		Editor& operator = (Editor&&) = delete;
		Editor& operator = (const Editor&) = delete;

		void create() override;

		std::pair<std::string, std::string> getDemoRestrictionText() const override;

		std::unique_ptr<jucePluginEditorLib::SettingsDeviceSpecific> createDeviceSpecificSettings(
			const std::string& _templateName, Rml::Element* _root) override;
		std::string getSettingsTemplateSuffix() const override;

		// Reapplies the configured wheel/encoder drag-speed percentages to the
		// panel knobs. Called on create and from the settings page.
		void applyPanelSpeeds();
		void applyPixelPerfectPanel();
		void applyLcdInteraction();
		void loadInstalledFactoryStorage();
		void chooseStorageImage();
		void restorePreviousStorage();
		bool hasStorageRecoveryImage() const;
		void chooseUserSysexFile();
		void cancelUserSysexTransfer();
		bool canResumeUserSysexTransfer() const;
		void resumeUserSysexTransfer();
		std::string getUserSysexMenuText() const;
		bool isUserSysexTransferActive() const;
		bool canCancelUserSysexTransfer() const;
		std::weak_ptr<void> getLifetimeToken() const { return m_lifetimeToken; }

		static constexpr int g_panelSpeedPercents[] = {50, 75, 100, 150, 200, 300};

	private:
		friend struct EditorIdentityTestAccess;

		void timerCallback(int _timerId) override;

		std::shared_ptr<md::FrontPanelPublisher> getFrontPanelPublisher();
		bool sendPanelEvent(uint8_t _command, uint8_t _argument) const;
		bool refreshFrontPanelState(double _nowMilliseconds);
		md::MachineModel getModel() const;
		void createLcd();
		void updateLcdInteractionState();
		std::optional<unsigned> lcdTargetAt(const Rml::Event& _event) const;
		void updateLcdHover(const Rml::Event& _event);
		void clearLcdHover();
		void cancelLcdGesture();
		void emitEncoderSteps(md::PanelEncoder _encoder, int _steps) const;
		void createButtons();
		void createPanelAffordances();
		void bindPanelTarget(const char* _id, md::PanelControl _control);
		void bindPanelChord(const char* _id, md::PanelControl _control);
		void pressPanelButton(juceRmlUi::ElemButton* _button, md::PanelControl _control,
			const md::PanelPacket& _packet, bool _shiftDown);
		void releasePanelButton(juceRmlUi::ElemButton* _button, md::PanelControl _control,
			const md::PanelPacket& _packet);
		void releaseActivePanelButtons();
		void beginPanelGesture(Rml::Element* _element,
			std::initializer_list<md::PanelControl> _controls);
		void endPanelGesture();
		void releasePanelButtonGestures();
		void releaseEncoderPress();
		void cancelPanelInputGestures();
		void releaseAllPanelInputs();
		void globalFocusChanged(juce::Component* _focusedComponent) override;
		void queuePanelPulse(md::PanelControl _control, int _count = 1);
		void servicePanelQueue();
		void servicePanelNavigation();
		void selectMachinedrumTrack(int _track);
		void selectMachinedrumDataPage(int _page);
		void selectMonomachineDataPage(int _page);
		void selectMonomachineTrigMode(int _mode);
		void togglePatternBankLatch(juceRmlUi::ElemButton* _button, const md::PanelPacket& _packet);
		void releasePatternBankLatch();
		void createEncoders();
		void createMasterVolume();
		void configureEncoder(juceRmlUi::ElemKnob* _knob, md::PanelEncoder _encoder,
			float& _last, float& _accum);
		void onEncoderChanged(juceRmlUi::ElemKnob* _knob, md::PanelEncoder _encoder,
			float& _last, float& _accum);
		void createLeds();
		bool updateLeds();
		void paintLcd(const juce::Image& _target, juce::Graphics& _graphics) const;

		enum class StorageImageBookmark
		{
			None,
			Factory,
			Other
		};

		void chooseStorageImage(StorageImageBookmark _bookmark);
		void confirmStorageImage(const juce::File& _file,
			StorageImageBookmark _bookmark);
		void showStorageOperationResult(bool _success, const juce::String& _message);
		std::optional<md::SysexImportProgress> getUserSysexProgress() const;
		void sendUserSysexFile(const juce::File& _file, const md::SysexImportTicket& _ticket);
		void startUserSysexTransfer(const std::shared_ptr<md::PreparedMidiSysexTransfer>& _prepared,
			const juce::File& _file, const md::SysexImportTicket& _ticket, bool _receiveModeConfirmed);
		void launchUserSysexFileChooser(const md::SysexImportTicket& _ticket);
		void showUserSysexError(const juce::String& _message);
		void serviceUserSysexProgress();

		enum class StorageImageFlow
		{
			None,
			Choosing,
			AwaitingConfirmation
		};

		Controller& m_controller;
		const md::MachineModel m_model;
		juceRmlUi::ElemCanvas* m_lcdCanvas = nullptr;
		std::unique_ptr<PixelPerfectPanel> m_pixelPerfectPanel;
		std::shared_ptr<md::FrontPanelPublisher> m_frontPanelPublisher;
		md::FrontPanel m_frontPanelSnapshot;
		bool m_frontPanelSnapshotValid = false;
		bool m_lcdChanged = true;
		bool m_lcdInteractionInputChanged = true;
		std::optional<lcdInteraction::State> m_lcdInteractionState;
		std::optional<unsigned> m_lcdHoverEncoder;
		std::optional<unsigned> m_lcdWheelEncoder;
		lcdInteraction::DragGesture m_lcdDragGesture;
		lcdInteraction::DetentAccumulator m_lcdWheelAccumulator;
		FrontPanelLedPresentation m_ledPresentation;
		bool m_ledsChanged = true;
		md::FrontPanelLedTransitionStatus m_ledTransitionStatus;
		bool m_ledTransitionStatusValid = false;
		bool m_ledResyncPending = false;
		uint64_t m_ledResyncSequence = 0;

		md::PanelRowState m_panelRows;
		juceRmlUi::ElemButton* m_patternBankButton = nullptr;
		std::optional<md::PanelPacket> m_patternBankPacket;
		Rml::Element* m_panelGestureElement = nullptr;
		std::vector<md::PanelPacket> m_panelGesturePackets;
		panelAffordances::ShiftPanelLatch m_shiftPanelLatch;
		panelAffordances::EncoderPressGesture m_encoderPress;
		juceRmlUi::ElemKnob* m_pressedEncoder = nullptr;

		struct ActivePanelButton
		{
			juceRmlUi::ElemButton* button = nullptr;
			md::PanelPacket packet;
		};
		std::vector<ActivePanelButton> m_activePanelButtons;

		struct PanelStep
		{
			md::PanelPacket packet;
			bool press = false;
		};
		std::deque<PanelStep> m_panelSteps;
		int m_panelSettleTicks = 0;
		panelAffordances::PendingTarget<panelAffordances::g_machinedrumDataPages.size()>
			m_machinedrumDataPageTarget;
		panelAffordances::PendingTarget<panelAffordances::g_monomachineDataPages.size()>
			m_monomachineDataPageTarget;
		panelAffordances::PendingTarget<panelAffordances::g_monomachineTrigModes.size()>
			m_monomachineTrigModeTarget;

		std::array<juceRmlUi::ElemKnob*, 8> m_encoders{};
		std::array<float, 8> m_encLast{};
		std::array<float, 8> m_encAccum{};
		juceRmlUi::ElemKnob* m_levelEncoder = nullptr;
		float m_levelLast = 0.0f;
		float m_levelAccum = 0.0f;
		juceRmlUi::ElemKnob* m_soundEncoder = nullptr;
		float m_soundLast = 0.0f;
		float m_soundAccum = 0.0f;
		juceRmlUi::ElemKnob* m_masterVolume = nullptr;

		std::array<Rml::Element*, 16> m_stepLeds{};
		std::array<Rml::Element*, 16> m_drumLeds{};

		struct StatusLedElem
		{
			Rml::Element* elem;
			uint8_t bit;	// md::FrontPanel::StatusLed
		};
		std::array<StatusLedElem, 5> m_statusLeds{};
		std::array<StatusLedElem, 6> m_mdModeLeds{};

		struct RawLedElem
		{
			Rml::Element* elem = nullptr;
			uint8_t bank = 0;
			uint8_t bit = 0;
		};
		std::array<RawLedElem, 4> m_mdPageLeds{};
		std::array<RawLedElem, 20> m_mmPanelLeds{};
		std::unique_ptr<juce::FileChooser> m_storageFileChooser;
		StorageImageFlow m_storageImageFlow = StorageImageFlow::None;
		std::unique_ptr<juce::FileChooser> m_sysexFileChooser;
		bool m_sysexChooserOpen = false;
		bool m_sysexTransferWasActive = false;
		md::SysexImportTicket m_sysexMonitoredTicket;
		md::MidiSysexTransferState m_sysexLastState =
			md::MidiSysexTransferState::Idle;
		size_t m_sysexLastSent = 0;
		uint32_t m_sysexLastServiceSerial = 0;
		uint32_t m_sysexReceivePromptId = 0;
		size_t m_sysexReceivePromptStep = 0;
		double m_sysexLastAdvanceMilliseconds = 0.0;
		bool m_sysexStallWarningShown = false;
		std::shared_ptr<void> m_lifetimeToken = std::make_shared<int>(0);
	};
}
