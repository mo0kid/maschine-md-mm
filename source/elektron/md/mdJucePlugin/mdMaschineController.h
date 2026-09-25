#pragma once

#include "mdMaschineNihiaClient.h"
#include "mdMaschineRecordedSteps.h"
#include "mdPluginProcessor.h"
#include "mdLib/mdpanel.h"

#include <atomic>
#include <array>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>

namespace mdJucePlugin::maschine
{
	class Controller
	{
	public:
		Controller(AudioPluginAudioProcessor& _machinedrum,
			AudioPluginAudioProcessor& _monomachine);
		~Controller();

		Controller(const Controller&) = delete;
		Controller& operator=(const Controller&) = delete;

		md::MachineModel focusedModel() const { return m_focused.load(); }
		uint8_t selectedMonomachineTrack();
		void setFocusedModel(md::MachineModel _model);

	private:
		enum class Lightshow : uint8_t { Random, Rainbow, Chase, Pulse, Count };
		void run();
		void handleButton(const nihia::ButtonEvent& _event);
		void handleKnob(const nihia::KnobEvent& _event);
		void handleMainKnob(const nihia::MainKnobEvent& _event);
		void handlePad(const nihia::PadEvent& _event);
		void sendControl(md::MachineModel _model, md::PanelControl _control,
			bool _pressed);
		void tapControl(md::MachineModel _model, md::PanelControl _control,
			unsigned _tapCount = 1);
		void pulseDisplayControl(uint8_t _slot, md::MachineModel _model,
			md::PanelControl _control);
		void sendEncoderPress(md::MachineModel _model, uint8_t _index,
			bool _pressed);
		void selectTrack(md::MachineModel _model, uint8_t _index);
		void toggleTrackMute(md::MachineModel _model, uint8_t _index);
		bool isTrackMuted(md::MachineModel _model, uint8_t _index);
		nihia::LedFrame buildLedFrame(const md::FrontPanel& _mdPanel,
			const md::FrontPanel& _mmPanel, uint8_t _mdTempoPulse,
			uint8_t _mmTempoPulse, bool _mmRecording, bool _mmGridRecording,
			bool _mdRecording, bool _mdGridRecording, unsigned _mdSelectedPage);
		md::MachineModel pressTarget(uint8_t _inputId, bool _pressed);
		md::MachineModel padTarget(uint8_t _padIndex, bool _pressed);
		AudioPluginAudioProcessor& processorFor(md::MachineModel _model);
		md::PanelRowState& rowsFor(md::MachineModel _model);
		AudioPluginAudioProcessor& focusedProcessor();

		AudioPluginAudioProcessor& m_machinedrum;
		AudioPluginAudioProcessor& m_monomachine;
		NihiaClient m_client;
		std::atomic<md::MachineModel> m_focused{md::MachineModel::Machinedrum};
		std::atomic<uint8_t> m_mmSelectedTrack{0};
		std::atomic<bool> m_mdPlaying{false};
		std::atomic<bool> m_mmPlaying{false};
		std::atomic<bool> m_mmRealtimeRecordGesture{false};
		std::atomic<bool> m_mdRealtimeRecordGesture{false};
		std::atomic<bool> m_stopping{false};
		std::mutex m_inputMutex;
		std::array<md::MachineModel, 128> m_buttonTargets{};
		std::array<md::MachineModel, 16> m_padTargets{};
		std::array<md::MachineModel, 8> m_encoderTouchTargets{};
		std::array<bool, 128> m_buttonPressed{};
		std::array<bool, 8> m_bankButtonSuppressed{};
		std::array<bool, 128> m_dualTransportGesture{};
		std::array<bool, 16> m_padPressed{};
		std::array<bool, 16> m_padSelectGesture{};
		std::array<bool, 16> m_padMuteGesture{};
		std::array<bool, 8> m_encoderTouched{};
		std::array<bool, 8> m_encoderTouchActive{};
		struct DisplayControlPulse
		{
			uint64_t deadlineMs = 0;
			unsigned pending = 0;
			bool pressed = false;
		};
		std::mutex m_displayPulseMutex;
		std::array<DisplayControlPulse, 6> m_displayPulses{};
		bool m_noteRepeatHeld = false;
		bool m_padModeHeld = false;
		bool m_muteHeld = false;
		bool m_mixerHeld = false;
		bool m_randomLightsEnabled = false;
		Lightshow m_lightshow = Lightshow::Random;
		int8_t m_knob5Direction = 0;
		uint8_t m_knob5DirectionRun = 0;
		uint64_t m_knob5LastEventMs = 0;
		bool m_shiftHeld = false;
		bool m_shiftFunctionForwarded = false;
		std::chrono::steady_clock::time_point m_recordPressedAt{};
		int m_mdPatternBankHeld = -1;
		int m_mdSelectedPatternBank = -1;
		int m_mdSelectedPatternSlot = -1;
		uint64_t m_mdLocalPatternSelectionMs = 0;
		uint8_t m_mdPatternBeforeLocalSelection = 0xff;
		bool m_mdPatternBankReleased = false;
		bool m_mdPatternBankScreenSeen = false;
		uint16_t m_mdPatternBankOccupied = 0;
		uint64_t m_mdPatternBankPressedMs = 0;
		uint64_t m_mdPatternBankReleasedMs = 0;
		int m_mmPatternBankHeld = -1;
		int m_mmSelectedPatternBank = -1;
		int m_mmSelectedPatternSlot = -1;
		uint64_t m_mmLocalPatternSelectionMs = 0;
		uint8_t m_mmPatternBeforeLocalSelection = 0xff;
		std::atomic<bool> m_mdBankGroupEH{false};
		std::atomic<bool> m_mmBankGroupEH{false};
		md::MachineModel m_shiftFunctionTarget = md::MachineModel::Machinedrum;
		md::PanelRowState m_mdRows;
		md::PanelRowState m_mmRows;
		std::mutex m_waitMutex;
		std::condition_variable m_waitCondition;
		std::thread m_thread;
	};
}
