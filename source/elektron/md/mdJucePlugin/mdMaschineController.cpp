#include "mdMaschineController.h"
#include "mdMaschineBankPreview.h"
#include "mdMaschineSectionPlayback.h"
#include "mdMaschineStripLeds.h"

#include "jucePluginLib/parameter.h"
#include "mdLib/mdautomation.h"
#include "mdLib/mdmidiprotocol.h"
#include "synthLib/midiTypes.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <optional>

namespace mdJucePlugin::maschine
{
	namespace
	{
		std::optional<md::PanelControl> mappedButton(const uint32_t _id)
		{
			switch(_id)
			{
			case 6: return md::PanelControl::Function;
			case 0: return md::PanelControl::Enter;
			case 2: return md::PanelControl::Up;
			case 3: return md::PanelControl::Right;
			case 4: return md::PanelControl::Down;
			case 5: return md::PanelControl::Left;
			case 8: case 12: return md::PanelControl::BankA;
			case 9: case 13: return md::PanelControl::BankB;
			case 10: case 14: return md::PanelControl::BankC;
			case 11: case 15: return md::PanelControl::BankD;
			case 19: return md::PanelControl::Tempo;
			case 24: return md::PanelControl::TrigSelect;
			case 27: return md::PanelControl::Scale;
			case 29: return md::PanelControl::SongEnable;
			case 30: return md::PanelControl::PatternSong;
			case 35: return md::PanelControl::Enter;
			case 38: return md::PanelControl::Enter; // PITCH / YES
			case 39: return md::PanelControl::Exit; // MOD / NO
			case 45: return md::PanelControl::Play;
			case 46: return md::PanelControl::Record;
			case 47: return md::PanelControl::Stop;
			case 50: return md::PanelControl::Right;
			case 53: return md::PanelControl::SynthesisEffectsRouting;
			case 56: return md::PanelControl::SynthesisEffectsRouting;
			case 57: return md::PanelControl::PatternSong;
			case 58: return md::PanelControl::Kit;
			case 59: return md::PanelControl::Left;
			case 65: case 69: return md::PanelControl::DataPageBackward;
			default: return {};
			}
		}

		std::optional<uint8_t> buttonLed(const uint32_t _id)
		{
			switch(_id)
			{
			case 56: return 1;  // Channel
			case 53: return 2;  // Plug-in
			case 57: return 3;  // Arranger
			case 58: return 4;  // Browser
			case 51: return 6;  // Sampling (indexed RGB)
			case 59: return 7;  // Left
			case 50: return 8;  // Right
			case 64: return 13; // Display 1
			case 65: return 14;
			case 66: return 15;
			case 67: return 16;
			case 68: return 17; // Display 5
			case 69: return 18;
			case 70: return 19;
			case 7: return 20;  // Display 8
			case 20: return 23; // Note Repeat
			case 19: return 24; // Tempo
			case 8: return 30;  // Group A
			case 9: return 31;
			case 10: return 32;
			case 11: return 33;
			case 12: return 34; // Group E
			case 13: return 35; // Group F
			case 14: return 36; // Group G
			case 15: return 37; // Group H
			case 45: return 42; // Play
			case 46: return 43; // Record
			case 47: return 44; // Stop
			case 6: return 45;  // Shift / Function
			case 24: return 47; // Pad Mode / Trig Select
			case 27: return 50; // Step / Scale
			case 29: return 51; // Scene / Song
			case 30: return 52; // Pattern
			case 35: return 56; // Select / Enter
			case 37: return 58; // Mute
			default: return {};
			}
		}

		constexpr std::array<uint8_t, 16> g_padLeds =
		{
			100, 101, 102, 103, 96, 97, 98, 99,
			92, 93, 94, 95, 88, 89, 90, 91,
		};

	}

	Controller::Controller(AudioPluginAudioProcessor& _machinedrum,
		AudioPluginAudioProcessor& _monomachine)
		: m_machinedrum(_machinedrum)
		, m_monomachine(_monomachine)
	{
		m_client.setButtonCallback([this](const nihia::ButtonEvent& _event)
		{
			handleButton(_event);
		});
		m_client.setKnobCallback([this](const nihia::KnobEvent& _event)
		{
			handleKnob(_event);
		});
		m_client.setMainKnobCallback([this](const nihia::MainKnobEvent& _event)
		{
			handleMainKnob(_event);
		});
		m_client.setPadCallback([this](const nihia::PadEvent& _event)
		{
			handlePad(_event);
		});
		m_thread = std::thread([this] { run(); });
	}

	Controller::~Controller()
	{
		m_client.setButtonCallback({});
		m_client.setKnobCallback({});
		m_client.setMainKnobCallback({});
		m_client.setPadCallback({});
		m_stopping.store(true);
		m_waitCondition.notify_all();
		if(m_thread.joinable())
			m_thread.join();
	}

	void Controller::setFocusedModel(const md::MachineModel _model)
	{
		m_focused.store(_model);
		m_waitCondition.notify_all();
	}

	AudioPluginAudioProcessor& Controller::focusedProcessor()
	{
		return m_focused.load() == md::MachineModel::Monomachine
			? m_monomachine : m_machinedrum;
	}

	AudioPluginAudioProcessor& Controller::processorFor(
		const md::MachineModel _model)
	{
		return _model == md::MachineModel::Monomachine
			? m_monomachine : m_machinedrum;
	}

	md::PanelRowState& Controller::rowsFor(const md::MachineModel _model)
	{
		return _model == md::MachineModel::Monomachine ? m_mmRows : m_mdRows;
	}

	md::MachineModel Controller::pressTarget(const uint8_t _inputId,
		const bool _pressed)
	{
		if(_pressed)
			m_buttonTargets[_inputId] = m_focused.load();
		return m_buttonTargets[_inputId];
	}

	md::MachineModel Controller::padTarget(const uint8_t _padIndex,
		const bool _pressed)
	{
		if(_pressed)
			m_padTargets[_padIndex] = m_focused.load();
		return m_padTargets[_padIndex];
	}

	void Controller::sendControl(const md::MachineModel _model,
		const md::PanelControl _control, const bool _pressed)
	{
		std::lock_guard lock(m_inputMutex);
		auto& processor = processorFor(_model);
		const auto packet = md::panelPacket(processor.getModel(), _control);
		if(!packet)
			return;
		const auto combined = _pressed
			? rowsFor(_model).press(*packet) : rowsFor(_model).release(*packet);
		(void)processor.sendPanelEvent(combined.row, combined.mask);
	}

	void Controller::tapControl(const md::MachineModel _model,
		const md::PanelControl _control, const unsigned _tapCount)
	{
		// The emulated panels are scanned by their firmware. A press and release
		// delivered in the same host callback can disappear between two scans.
		// Keep each edge visible for several scans, including between repeated taps.
		for(unsigned tap = 0; tap < _tapCount; ++tap)
		{
			sendControl(_model, _control, true);
			std::this_thread::sleep_for(std::chrono::milliseconds(66));
			sendControl(_model, _control, false);
			if(tap + 1 < _tapCount)
				std::this_thread::sleep_for(std::chrono::milliseconds(33));
		}
	}

	void Controller::pulseDisplayControl(const uint8_t _slot,
		const md::MachineModel _model, const md::PanelControl _control)
	{
		if(_slot >= m_displayControlReleaseMs.size())
			return;
		// Hold the emulated key across more than one controller refresh so the
		// firmware's panel scan always observes both edges.
		if(m_displayControlReleaseMs[_slot].exchange(0) != 0)
			sendControl(_model, _control, false);
		sendControl(_model, _control, true);
		const auto now = std::chrono::steady_clock::now();
		const auto nowMs = static_cast<uint64_t>(
			std::chrono::duration_cast<std::chrono::milliseconds>(
				now.time_since_epoch()).count());
		m_displayControlReleaseMs[_slot].store(nowMs + 66);
		m_waitCondition.notify_all();
	}

	void Controller::sendEncoderPress(const md::MachineModel _model,
		const uint8_t _index, const bool _pressed)
	{
		if(_index >= 8)
			return;
		std::lock_guard lock(m_inputMutex);
		auto& processor = processorFor(_model);
		const auto packet = md::panelEncoderPressPacket(processor.getModel(),
			static_cast<md::PanelEncoder>(_index));
		if(!packet)
			return;
		const auto combined = _pressed
			? rowsFor(_model).press(*packet) : rowsFor(_model).release(*packet);
		(void)processor.sendPanelEvent(combined.row, combined.mask);
	}

	void Controller::selectTrack(const md::MachineModel _model,
		const uint8_t _index)
	{
		if(_model == md::MachineModel::Machinedrum)
		{
			if(_index >= 16)
				return;
			const auto body = md::midiProtocol::selectTrack(_index);
			synthLib::SMidiEvent event(synthLib::MidiEventSource::Editor);
			event.sysex.reserve(body.size() + 2);
			event.sysex.push_back(0xf0);
			event.sysex.insert(event.sysex.end(), body.begin(), body.end());
			event.sysex.push_back(0xf7);
			m_machinedrum.addMidiEvent(event);
			return;
		}

		if(_index >= 6)
			return;
		m_mmSelectedTrack.store(_index);
		const auto control = static_cast<md::PanelControl>(
			static_cast<uint8_t>(md::PanelControl::Track1) + _index);
		sendControl(_model, control, true);
		sendControl(_model, control, false);
	}

	void Controller::toggleTrackMute(const md::MachineModel _model,
		const uint8_t _index)
	{
		const auto trackCount = _model == md::MachineModel::Monomachine
			? md::automation::monomachine::TrackCount
			: md::automation::machinedrum::TrackCount;
		if(_index >= trackCount)
			return;
		const auto mutePage = _model == md::MachineModel::Monomachine
			? md::automation::monomachine::Mute
			: md::automation::machinedrum::Mute;
		for(auto* const audioParameter : processorFor(_model).getParameters())
		{
			auto* const parameter =
				dynamic_cast<pluginLib::Parameter*>(audioParameter);
			if(!parameter || parameter->getPart() != _index)
				continue;
			const auto& description = parameter->getDescription();
			if(description.page != mutePage || description.index != 0)
				continue;
			parameter->setUnnormalizedValueNotifyingHost(
				parameter->getUnnormalizedValue() == 0 ? 1 : 0,
				pluginLib::Parameter::Origin::Ui);
			return;
		}
	}

	bool Controller::isTrackMuted(const md::MachineModel _model,
		const uint8_t _index)
	{
		const auto mutePage = _model == md::MachineModel::Monomachine
			? md::automation::monomachine::Mute
			: md::automation::machinedrum::Mute;
		for(auto* const audioParameter : processorFor(_model).getParameters())
		{
			auto* const parameter =
				dynamic_cast<pluginLib::Parameter*>(audioParameter);
			if(parameter && parameter->getPart() == _index
				&& parameter->getDescription().page == mutePage
				&& parameter->getDescription().index == 0)
				return parameter->getUnnormalizedValue() != 0;
		}
		return false;
	}

	void Controller::handleButton(const nihia::ButtonEvent& _event)
	{
		bool wasPressed = false;
		if(_event.id < m_buttonPressed.size())
		{
			std::lock_guard lock(m_inputMutex);
			wasPressed = m_buttonPressed[_event.id];
			m_buttonPressed[_event.id] = _event.pressed;
			if(_event.pressed && !wasPressed && (_event.id == 45 || _event.id == 46)
				&& m_buttonPressed[45] && m_buttonPressed[46])
				(m_focused.load() == md::MachineModel::Monomachine
					? m_mmRealtimeRecordGesture : m_mdRealtimeRecordGesture).store(true);
		}

		if(_event.pressed && _event.id == 64)
		{
			setFocusedModel(md::MachineModel::Machinedrum);
			return;
		}
		if(_event.pressed && _event.id == 68)
		{
			setFocusedModel(md::MachineModel::Monomachine);
			return;
		}
		if(_event.id == 65 || _event.id == 69)
		{
			// The second button above each display advances that instrument's
			// edit page, independent of controller focus.
			if(_event.pressed)
			{
				const auto target = _event.id == 65
					? md::MachineModel::Machinedrum
					: md::MachineModel::Monomachine;
				const auto control = target == md::MachineModel::Machinedrum
					? md::PanelControl::SynthesisEffectsRouting
					: md::PanelControl::DataPageForward;
				pulseDisplayControl(_event.id == 65 ? 0 : 2, target, control);
			}
			m_waitCondition.notify_all();
			return;
		}
		if(_event.id == 66 || _event.id == 70)
		{
			// The third button above each display advances that display's native
			// 1:4..4:4 page selector, independent of controller focus. Complete
			// the tap on the press event because Maschine's display-button release
			// reports are not reliable in every controller mode.
			if(_event.pressed)
			{
				const auto target = _event.id == 66
					? md::MachineModel::Machinedrum
					: md::MachineModel::Monomachine;
				pulseDisplayControl(_event.id == 66 ? 1 : 3, target,
					md::PanelControl::Scale);
			}
			m_waitCondition.notify_all();
			return;
		}
		if(_event.id == 67 || _event.id == 7)
		{
			// The fourth button over each display changes the model-specific mode
			// shown at the far right of that display's header.
			if(_event.pressed)
			{
				const auto target = _event.id == 67
					? md::MachineModel::Machinedrum
					: md::MachineModel::Monomachine;
				const auto control = target == md::MachineModel::Machinedrum
					? md::PanelControl::ClassicExtended
					: md::PanelControl::TrigSelect;
				pulseDisplayControl(_event.id == 67 ? 4 : 5, target, control);
			}
			m_waitCondition.notify_all();
			return;
		}
		if(_event.id == 59 || _event.id == 50)
		{
			// Maschine's dedicated < and > buttons browse edit pages. MM has
			// native previous/next controls; MD's SYNTHESIS/EFFECTS/ROUTING key
			// only advances, so two forward taps implement one backward step.
			if(_event.pressed)
			{
				const auto target = m_focused.load();
				if(target == md::MachineModel::Monomachine)
				{
					const auto control = _event.id == 50
						? md::PanelControl::DataPageForward
						: md::PanelControl::DataPageBackward;
					tapControl(target, control);
				}
				else
					tapControl(target,
						md::PanelControl::SynthesisEffectsRouting,
						_event.id == 50 ? 1u : 2u);
			}
			m_waitCondition.notify_all();
			return;
		}
		if(_event.id == 20)
		{
			std::array<md::MachineModel, 8> targets{};
			std::array<bool, 8> actions{};
			{
				std::lock_guard lock(m_inputMutex);
				m_noteRepeatHeld = _event.pressed;
				for(size_t i = 0; i < m_encoderTouched.size(); ++i)
				{
					if(_event.pressed && m_encoderTouched[i]
						&& !m_encoderTouchActive[i])
					{
						m_encoderTouchActive[i] = true;
						m_encoderTouchTargets[i] = m_focused.load();
						targets[i] = m_encoderTouchTargets[i];
						actions[i] = true;
					}
					else if(!_event.pressed && m_encoderTouchActive[i])
					{
						targets[i] = m_encoderTouchTargets[i];
						m_encoderTouchActive[i] = false;
						actions[i] = true;
					}
				}
			}
			for(size_t i = 0; i < actions.size(); ++i)
				if(actions[i])
					sendEncoderPress(targets[i], static_cast<uint8_t>(i),
						_event.pressed);
			m_waitCondition.notify_all();
			return;
		}
		if(_event.id == 6)
		{
			md::MachineModel target{};
			bool forward = false;
			{
				std::lock_guard lock(m_inputMutex);
				m_shiftHeld = _event.pressed;
				if(_event.pressed)
				{
					m_shiftFunctionTarget = m_focused.load();
					m_shiftFunctionForwarded = true;
					target = m_shiftFunctionTarget;
					forward = true;
				}
				else if(m_shiftFunctionForwarded)
				{
					target = m_shiftFunctionTarget;
					m_shiftFunctionForwarded = false;
					forward = true;
				}
			}
			if(forward)
				sendControl(target, md::PanelControl::Function, _event.pressed);
			m_waitCondition.notify_all();
			return;
		}
		if(_event.id == 24)
		{
			std::lock_guard lock(m_inputMutex);
			m_padModeHeld = _event.pressed;
			m_waitCondition.notify_all();
			return;
		}
		if(_event.id == 37)
		{
			std::lock_guard lock(m_inputMutex);
			m_muteHeld = _event.pressed;
			m_waitCondition.notify_all();
			return;
		}
		if(_event.id == 52)
		{
			std::lock_guard lock(m_inputMutex);
			m_mixerHeld = _event.pressed;
			m_waitCondition.notify_all();
			return;
		}
		// Shift + Sampling toggles decorative lighting. Instrument mode actions
		// remain on display buttons 4 and 8.
		if(_event.id == 51)
		{
			std::lock_guard lock(m_inputMutex);
			if(_event.pressed && !wasPressed && m_shiftHeld)
				m_randomLightsEnabled = !m_randomLightsEnabled;
			m_waitCondition.notify_all();
			return;
		}
		if(_event.id == 43 || _event.id == 33)
		{
			// ERase and DUPLICATE provide the two native FUNCTION shortcuts that
			// Shift+Play/Stop deliberately repurpose for dual-machine transport.
			const auto target = pressTarget(_event.id, _event.pressed);
			const auto action = _event.id == 43
				? md::PanelControl::Play   // FUNCTION + PLAY = CLEAR
				: md::PanelControl::Stop;  // FUNCTION + STOP = PASTE
			if(_event.pressed)
			{
				sendControl(target, md::PanelControl::Function, true);
				sendControl(target, action, true);
			}
			else
			{
				sendControl(target, action, false);
				sendControl(target, md::PanelControl::Function, false);
			}
			m_waitCondition.notify_all();
			return;
		}
		if(_event.id == 46)
		{
			// Keep quick taps down long enough for the firmware to scan them.
			// This also lets a busy panel-input queue recover the press state
			// before its release, while preserving held Record+Play gestures.
			if(wasPressed == _event.pressed)
				return;
			md::MachineModel target;
			std::chrono::steady_clock::time_point pressedAt;
			{
				std::lock_guard lock(m_inputMutex);
				target = pressTarget(_event.id, _event.pressed);
				if(_event.pressed)
					m_recordPressedAt = std::chrono::steady_clock::now();
				pressedAt = m_recordPressedAt;
			}
			if(!_event.pressed)
				std::this_thread::sleep_until(
					pressedAt + std::chrono::milliseconds(100));
			sendControl(target, md::PanelControl::Record, _event.pressed);
			m_waitCondition.notify_all();
			return;
		}
		if(_event.id == 45 || _event.id == 47)
		{
			bool dual = false;
			bool releaseFunction = false;
			md::MachineModel functionTarget{};
			{
				std::lock_guard lock(m_inputMutex);
				if(_event.pressed)
					m_dualTransportGesture[_event.id] = m_shiftHeld;
				dual = m_dualTransportGesture[_event.id];
				if(!_event.pressed)
					m_dualTransportGesture[_event.id] = false;
				if(dual && m_shiftFunctionForwarded)
				{
					releaseFunction = true;
					functionTarget = m_shiftFunctionTarget;
					m_shiftFunctionForwarded = false;
				}
			}
			if(dual)
			{
				if(releaseFunction)
					sendControl(functionTarget, md::PanelControl::Function, false);
				const auto control = _event.id == 45
					? md::PanelControl::Play : md::PanelControl::Stop;
				sendControl(md::MachineModel::Machinedrum, control, _event.pressed);
				sendControl(md::MachineModel::Monomachine, control, _event.pressed);
				if(_event.pressed)
				{
					m_mdPlaying.store(_event.id == 45);
					m_mmPlaying.store(_event.id == 45);
				}
				m_waitCondition.notify_all();
				return;
			}
		}
		if(_event.id >= 72 && _event.id <= 79)
		{
			const auto index = static_cast<uint8_t>(79 - _event.id);
			md::MachineModel target{};
			bool send = false;
			bool pressed = false;
			{
				std::lock_guard lock(m_inputMutex);
				m_encoderTouched[index] = _event.pressed;
				if(_event.pressed && m_noteRepeatHeld
					&& !m_encoderTouchActive[index])
				{
					m_encoderTouchActive[index] = true;
					m_encoderTouchTargets[index] = m_focused.load();
					target = m_encoderTouchTargets[index];
					pressed = true;
					send = true;
				}
				else if(!_event.pressed && m_encoderTouchActive[index])
				{
					target = m_encoderTouchTargets[index];
					m_encoderTouchActive[index] = false;
					send = true;
				}
			}
			if(send)
				sendEncoderPress(target, index, pressed);
			m_waitCondition.notify_all();
			return;
		}
		const auto control = mappedButton(_event.id);
		if(!control)
			return;
		md::MachineModel target;
		bool toggleBankGroup = false;
		bool suppressBankButton = false;
		{
			std::lock_guard lock(m_inputMutex);
			target = pressTarget(_event.id, _event.pressed);
			if(_event.id >= 8 && _event.id <= 15)
			{
				const auto bankButton = static_cast<size_t>(_event.id - 8);
				if(_event.pressed)
					m_bankButtonSuppressed[bankButton] = m_shiftHeld && _event.id >= 12;
				suppressBankButton = m_bankButtonSuppressed[bankButton];
				if(!_event.pressed)
					m_bankButtonSuppressed[bankButton] = false;
				auto& held = target == md::MachineModel::Machinedrum
					? m_mdPatternBankHeld : m_mmPatternBankHeld;
				// FUNCTION + A/E, B/F, C/G and D/H are the synth's
				// alternate functions, not pattern-bank selections.
				if(_event.pressed && !m_shiftHeld)
				{
					const bool desiredEH = _event.id >= 12;
					auto& groupEH = target == md::MachineModel::Machinedrum
						? m_mdBankGroupEH : m_mmBankGroupEH;
					toggleBankGroup = groupEH.load() != desiredEH;
					if(toggleBankGroup)
						groupEH.store(desiredEH);
					held = static_cast<int>(_event.id - 8);
					if(target == md::MachineModel::Machinedrum)
					{
						m_mdPatternBankReleased = false;
						m_mdPatternBankScreenSeen = false;
						m_mdPatternBankOccupied = 0;
						m_mdPatternBankPressedMs = static_cast<uint64_t>(
							std::chrono::duration_cast<std::chrono::milliseconds>(
								std::chrono::steady_clock::now().time_since_epoch()).count());
					}
				}
				else if(!_event.pressed
					&& target == md::MachineModel::Machinedrum
					&& held >= 0)
				{
					// Keep the MD pattern preview alive after the physical bank key
					// is released. The controller thread clears it when the native
					// LCD bank-selection overlay itself disappears.
					m_mdPatternBankReleased = true;
					m_mdPatternBankScreenSeen = false;
					m_mdPatternBankReleasedMs = static_cast<uint64_t>(
						std::chrono::duration_cast<std::chrono::milliseconds>(
							std::chrono::steady_clock::now().time_since_epoch()).count());
				}
				else
					held = -1;
			}
		}
		if(suppressBankButton)
		{
			m_waitCondition.notify_all();
			return;
		}
		if(_event.pressed && *control == md::PanelControl::Play)
			(target == md::MachineModel::Monomachine
				? m_mmPlaying : m_mdPlaying).store(true);
		if(_event.pressed && *control == md::PanelControl::Stop)
			(target == md::MachineModel::Monomachine
				? m_mmPlaying : m_mdPlaying).store(false);
		if(toggleBankGroup)
			tapControl(target, md::PanelControl::BankGroup);
		sendControl(target, *control, _event.pressed);
		m_waitCondition.notify_all();
	}

	void Controller::handleKnob(const nihia::KnobEvent& _event)
	{
		if(_event.index >= 8 || _event.rotation == 0)
			return;
		std::lock_guard lock(m_inputMutex);
		if(_event.index == 4)
		{
			// This particular hardware encoder is noisy. Require two consecutive
			// reports in the same direction before moving a value; isolated +/-
			// oscillations disappear without changing the feel of the other knobs.
			const auto nowMs = static_cast<uint64_t>(
				std::chrono::duration_cast<std::chrono::milliseconds>(
					std::chrono::steady_clock::now().time_since_epoch()).count());
			const auto direction = static_cast<int8_t>(_event.rotation > 0 ? 1 : -1);
			if(nowMs - m_knob5LastEventMs > 120 || direction != m_knob5Direction)
			{
				m_knob5Direction = direction;
				m_knob5DirectionRun = 1;
			}
			else
				m_knob5DirectionRun = static_cast<uint8_t>(
					std::min<unsigned>(m_knob5DirectionRun + 1, 0xff));
			m_knob5LastEventMs = nowMs;
			if(m_knob5DirectionRun < 2)
				return;
		}
		AudioPluginAudioProcessor* processor = nullptr;
		md::PanelEncoder encoder{};
		if(m_mixerHeld)
		{
			// MIXER exposes the two gain controls of the focused instrument:
			// encoder 1 is its selected-track LEVEL/DATA control, while encoder 8
			// adjusts the existing software master-volume control shown at the
			// top-left of the instrument panel.
			if(_event.index == 0)
			{
				processor = &focusedProcessor();
				encoder = md::PanelEncoder::Level;
			}
			else if(_event.index == 7)
			{
				auto& focused = focusedProcessor();
				const auto delta = _event.rotation > 0 ? 0.01f : -0.01f;
				focused.setOutputGain(std::clamp(
					focused.getOutputGain() + delta, 0.0f, 1.0f));
				m_waitCondition.notify_all();
				return;
			}
			else
				return;
		}
		else
		{
			processor = &focusedProcessor();
			encoder = static_cast<md::PanelEncoder>(_event.index);
		}
		const auto command = md::panelEncoderCommand(processor->getModel(), encoder);
		if(command)
			(void)processor->sendPanelEvent(*command,
				_event.rotation > 0 ? uint8_t{1} : uint8_t{0xff});
	}

	void Controller::handleMainKnob(const nihia::MainKnobEvent& _event)
	{
		if(_event.rotation == 0)
			return;
		std::lock_guard lock(m_inputMutex);
		if(m_buttonPressed[51])
		{
			constexpr int count = static_cast<int>(Lightshow::Count);
			const int direction = _event.rotation > 0 ? 1 : -1;
			m_lightshow = static_cast<Lightshow>(
				(static_cast<int>(m_lightshow) + direction + count) % count);
			m_waitCondition.notify_all();
			return;
		}
		auto& processor = focusedProcessor();
		// Maschine's 5D encoder stands in for each instrument's large contextual
		// wheel. On MD that is SOUND SELECTION (also used by the tempo screen),
		// whereas MM uses its LEVEL encoder for the same contextual editing role.
		const auto encoder = processor.getModel() == md::MachineModel::Machinedrum
			? md::PanelEncoder::SoundSelection
			: md::PanelEncoder::Level;
		const auto command = md::panelEncoderCommand(processor.getModel(), encoder);
		if(!command)
			return;
		(void)processor.sendPanelEvent(*command,
			_event.rotation > 0 ? uint8_t{1} : uint8_t{0xff});
	}

	void Controller::handlePad(const nihia::PadEvent& _event)
	{
		if(_event.index >= 16)
			return;
		md::MachineModel target;
		bool selectGesture = false;
		bool muteGesture = false;
		{
			std::lock_guard lock(m_inputMutex);
			const bool wasPressed = m_padPressed[_event.index];
			m_padPressed[_event.index] = _event.pressed;
			const bool stateChanged = wasPressed != _event.pressed;
			if(!stateChanged)
				return;
			target = padTarget(_event.index, _event.pressed);
			if(_event.pressed && !wasPressed)
			{
				m_padMuteGesture[_event.index] = m_muteHeld;
				m_padSelectGesture[_event.index] = !m_muteHeld && m_padModeHeld;
			}
			selectGesture = m_padSelectGesture[_event.index];
			muteGesture = m_padMuteGesture[_event.index];
			const auto nowMs = static_cast<uint64_t>(
				std::chrono::duration_cast<std::chrono::milliseconds>(
					std::chrono::steady_clock::now().time_since_epoch()).count());
			if(_event.pressed && !selectGesture
				&& target == md::MachineModel::Machinedrum
				&& m_mdPatternBankHeld >= 0)
			{
				m_mdPatternBeforeLocalSelection =
					m_machinedrum.getCurrentPattern();
				m_mdSelectedPatternBank = m_mdPatternBankHeld;
				m_mdSelectedPatternSlot = _event.index;
				m_mdLocalPatternSelectionMs = nowMs;
			}
			else if(_event.pressed && !selectGesture
				&& target == md::MachineModel::Monomachine
				&& m_mmPatternBankHeld >= 0)
			{
				m_mmPatternBeforeLocalSelection =
					m_monomachine.getCurrentPattern();
				m_mmSelectedPatternBank = m_mmPatternBankHeld;
				m_mmSelectedPatternSlot = _event.index;
				m_mmLocalPatternSelectionMs = nowMs;
			}
			if(!_event.pressed)
			{
				m_padSelectGesture[_event.index] = false;
				m_padMuteGesture[_event.index] = false;
			}
		}
		if(muteGesture)
		{
			if(_event.pressed)
				toggleTrackMute(target, _event.index);
			m_waitCondition.notify_all();
			return;
		}
		if(selectGesture)
		{
			if(_event.pressed)
				selectTrack(target, _event.index);
			m_waitCondition.notify_all();
			return;
		}
		sendControl(target, static_cast<md::PanelControl>(
			static_cast<uint8_t>(md::PanelControl::Trigger1) + _event.index),
			_event.pressed);
		m_waitCondition.notify_all();
	}

	nihia::LedFrame Controller::buildLedFrame(const md::FrontPanel& _mdPanel,
		const md::FrontPanel& _mmPanel, const uint8_t _mdTempoPulse,
		const uint8_t _mmTempoPulse, const bool _mmRecording, const bool _mmGridRecording,
		const bool _mdRecording, const bool _mdGridRecording, const unsigned _mdSelectedPage)
	{
		nihia::LedFrame result{};
		std::array<bool, 128> buttons{};
		std::array<bool, 16> pads{};
		bool noteRepeat = false;
		bool padMode = false;
		bool muteMode = false;
		bool mixer = false;
		bool randomLights = false;
		Lightshow lightshow = Lightshow::Random;
		int mdPatternBankHeld = -1;
		int mdSelectedPatternBank = -1;
		int mdSelectedPatternSlot = -1;
		uint16_t mdPatternBankOccupied = 0;
		int mmPatternBankHeld = -1;
		int mmSelectedPatternBank = -1;
		int mmSelectedPatternSlot = -1;
		const auto mdCurrentPattern = m_machinedrum.getCurrentPattern();
		const auto mmCurrentPattern = m_monomachine.getCurrentPattern();
		{
			std::lock_guard lock(m_inputMutex);
			// The firmware pattern-status poll is intentionally lightweight (5 s).
			// Keep an immediately selected pad authoritative while the poll still
			// reports the value observed before that selection. Accept confirmation
			// of the local selection, or a genuinely different later change.
			const auto updatePatternSelection = [](const uint8_t _currentPattern,
				int& _bank, int& _slot, uint64_t& _localSelectionMs,
				uint8_t& _patternBeforeLocalSelection)
			{
				if(_currentPattern >= 128)
					return;
				const bool localSelectionPending = _localSelectionMs != 0;
				const bool confirmsLocalSelection = _bank == _currentPattern / 16
					&& _slot == _currentPattern % 16;
				const bool reportsNewerSelection = localSelectionPending
					&& _currentPattern != _patternBeforeLocalSelection;
				if(!localSelectionPending || confirmsLocalSelection
					|| reportsNewerSelection)
				{
					_bank = _currentPattern / 16;
					_slot = _currentPattern % 16;
					_localSelectionMs = 0;
					_patternBeforeLocalSelection = 0xff;
				}
			};
			updatePatternSelection(mdCurrentPattern, m_mdSelectedPatternBank,
				m_mdSelectedPatternSlot, m_mdLocalPatternSelectionMs,
				m_mdPatternBeforeLocalSelection);
			updatePatternSelection(mmCurrentPattern, m_mmSelectedPatternBank,
				m_mmSelectedPatternSlot, m_mmLocalPatternSelectionMs,
				m_mmPatternBeforeLocalSelection);
			buttons = m_buttonPressed;
			pads = m_padPressed;
			noteRepeat = m_noteRepeatHeld;
			padMode = m_padModeHeld;
			muteMode = m_muteHeld;
			mixer = m_mixerHeld;
			randomLights = m_randomLightsEnabled;
			lightshow = m_lightshow;
			mdPatternBankHeld = m_mdPatternBankHeld;
			mdSelectedPatternBank = m_mdSelectedPatternBank;
			mdSelectedPatternSlot = m_mdSelectedPatternSlot;
			mdPatternBankOccupied = m_mdPatternBankOccupied;
			mmPatternBankHeld = m_mmPatternBankHeld;
			mmSelectedPatternBank = m_mmSelectedPatternBank;
			mmSelectedPatternSlot = m_mmSelectedPatternSlot;
		}

		const auto focused = m_focused.load();
		const auto& panel = focused == md::MachineModel::Monomachine
			? _mmPanel : _mdPanel;
		setStripLeds(result, panel, focused);
		const auto accent = focused == md::MachineModel::Monomachine
			? nihia::LedColor::Mint : nihia::LedColor::Orange;

		for(uint32_t id = 0; id < buttons.size(); ++id)
			if(const auto led = buttonLed(id))
				nihia::setLed(result, *led, nihia::LedColor::White,
					buttons[id] ? 3 : 1);

		// Browser is the entry point to the custom controller surface. Keep it
		// clearly visible even when it is not being pressed.
		nihia::setLed(result, 4, nihia::LedColor::Cyan,
			buttons[58] ? 3 : 2);

		// Sampling continuously sweeps the palette; Shift + Sampling adds an
		// an effect to otherwise idle banks and pads. Sampling + the 5D dial
		// selects its style without affecting the instrument's contextual wheel.
		const auto animationMs = static_cast<uint64_t>(
			std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::steady_clock::now().time_since_epoch()).count());
		const auto samplingColor = static_cast<nihia::LedColor>(
			1u + static_cast<uint8_t>((animationMs / 80u) % 16u));
		nihia::setLed(result, 6, samplingColor, buttons[51] ? 3 : 2);
		const auto animateLight = [&](const uint8_t _led, const uint8_t _position,
			const uint8_t _count)
		{
			const auto hue = static_cast<uint8_t>((animationMs / 100u
				+ _position * 16u / _count) % 16u);
			switch(lightshow)
			{
			case Lightshow::Rainbow:
				nihia::setLed(result, _led, static_cast<nihia::LedColor>(1u + hue), 2);
				return;
			case Lightshow::Chase:
			{
				const auto head = static_cast<uint8_t>((animationMs / 80u) % _count);
				const auto distance = static_cast<uint8_t>((head + _count - _position) % _count);
				if(distance >= 3)
					nihia::setLed(result, _led, nihia::LedColor::Off, 0);
				else
					nihia::setLed(result, _led, static_cast<nihia::LedColor>(1u + hue),
						static_cast<uint8_t>(3 - distance));
				return;
			}
			case Lightshow::Pulse:
			{
				constexpr uint8_t levels[] = {0, 1, 2, 3, 2, 1};
				const auto phase = (animationMs / 100u + _position) % std::size(levels);
				nihia::setLed(result, _led, static_cast<nihia::LedColor>(1u + hue),
					levels[phase]);
				return;
			}
			default: break;
			}
			// Hash each LED's own time slot so adjacent lights neither share colours
			// nor change in lockstep. No random generator or allocation on this path.
			uint32_t value = static_cast<uint32_t>(
				(animationMs + _led * 37u) / 100u) ^ (_led * 0x9e3779b9u);
			value ^= value >> 16;
			value *= 0x7feb352du;
			value ^= value >> 15;
			value *= 0x846ca68bu;
			value ^= value >> 16;
			nihia::setLed(result, _led,
				static_cast<nihia::LedColor>(1u + (value & 15u)),
				static_cast<uint8_t>(1u + ((value >> 4) % 3u)));
		};

		// The ring around the large encoder has independent directional LEDs.
		// Keep them faintly visible and brighten the direction being pressed.
		nihia::setLed(result, 59, accent, buttons[2] ? 3 : 1); // Up
		nihia::setLed(result, 60, accent, buttons[5] ? 3 : 1); // Left
		nihia::setLed(result, 61, accent, buttons[3] ? 3 : 1); // Right
		nihia::setLed(result, 62, accent, buttons[4] ? 3 : 1); // Down

		// The two focus keys remain visible in their machine colors.
		nihia::setLed(result, 13, nihia::LedColor::Orange,
			focused == md::MachineModel::Machinedrum ? 3 : 1);
		nihia::setLed(result, 17, nihia::LedColor::Mint,
			focused == md::MachineModel::Monomachine ? 3 : 1);

		// Maschine Group A-H map directly to Elektron pattern banks A-H.
		for(uint8_t led = 30; led <= 37; ++led)
		{
			nihia::setLed(result, led, accent,
				buttons[8 + led - 30] ? 3 : 1);
			if(randomLights && !buttons[8 + led - 30])
				animateLight(led, static_cast<uint8_t>(led - 30), 8);
		}

		const auto active = [&](const uint32_t id, const bool state,
			const nihia::LedColor color)
		{
			if(const auto led = buttonLed(id); led && (state || buttons[id]))
				nihia::setLed(result, *led, color, 3);
		};
		active(20, noteRepeat, nihia::LedColor::Yellow);
		active(37, muteMode, nihia::LedColor::Red);
		active(52, mixer, nihia::LedColor::Yellow);
		const auto mdFocused = focused == md::MachineModel::Machinedrum;
		// MD's named 0x22/0x23 banks contain these mode LEDs. On MM the
		// same bank addresses are bicolour trigger data, so do not present
		// them as mode feedback until the extended MM bank map is verified.
		active(19, mdFocused
			&& panel.getModeLed(md::FrontPanel::ModeLed::Tempo), accent);
		const auto rawLed = [](const md::FrontPanel& _frontPanel,
			const uint8_t _bank, const uint8_t _bit)
		{
			return ((_frontPanel.getLedBankRaw(_bank) >> _bit) & 1u) == 0;
		};
		m_mdBankGroupEH.store(_mdPanel.getModeLed(
			md::FrontPanel::ModeLed::BankGroupEH));
		m_mmBankGroupEH.store(rawLed(_mmPanel, 0x26, 4));
		active(29, mdFocused
			&& panel.getStatusLed(md::FrontPanel::StatusLed::Song), accent);
		active(30, mdFocused
			&& panel.getStatusLed(md::FrontPanel::StatusLed::Pattern), accent);
		active(45, focused == md::MachineModel::Monomachine
			? m_mmPlaying.load() : m_mdPlaying.load(), nihia::LedColor::Green);
		active(46, mdFocused
			&& panel.getModeLed(md::FrontPanel::ModeLed::Record),
			nihia::LedColor::Red);
		active(53, mdFocused && (panel.getStatusLed(md::FrontPanel::StatusLed::Routing)
			|| panel.getStatusLed(md::FrontPanel::StatusLed::Effects)
			|| panel.getStatusLed(md::FrontPanel::StatusLed::Synthesis)), accent);
		const auto tempoPulse = focused == md::MachineModel::Monomachine
			? _mmTempoPulse : _mdTempoPulse;
		if(tempoPulse != 0)
			nihia::setLed(result, 24, nihia::LedColor::White, tempoPulse);

		active(24, padMode, nihia::LedColor::Yellow);

		const auto mmTrackColor = [&](const size_t _track)
		{
			if(_track >= 6)
				return md::FrontPanel::LedColor::Off;
			constexpr std::array<uint8_t, 6> banks
				{0x25, 0x25, 0x24, 0x24, 0x24, 0x24};
			constexpr std::array<uint8_t, 6> greenBits{0, 2, 0, 2, 4, 6};
			const auto raw = _mmPanel.getLedBankRaw(banks[_track]);
			const auto greenBit = greenBits[_track];
			const bool green = ((raw >> greenBit) & 1u) == 0;
			const bool red = ((raw >> (greenBit + 1)) & 1u) == 0;
			if(green && red)
				return md::FrontPanel::LedColor::Yellow;
			if(green)
				return md::FrontPanel::LedColor::Green;
			if(red)
				return md::FrontPanel::LedColor::Red;
			return md::FrontPanel::LedColor::Off;
		};

		for(size_t i = 0; i < pads.size(); ++i)
		{
			auto color = accent;
			bool lit = false;
			if(muteMode)
			{
				const auto trackCount = focused == md::MachineModel::Monomachine
					? md::automation::monomachine::TrackCount
					: md::automation::machinedrum::TrackCount;
				if(i >= trackCount)
				{
					nihia::setLed(result, g_padLeds[i],
						nihia::LedColor::Off, 0);
					continue;
				}
				lit = isTrackMuted(focused, static_cast<uint8_t>(i));
				color = lit ? nihia::LedColor::Red : accent;
			}
			else if(padMode)
			{
				if(focused == md::MachineModel::Monomachine)
				{
					if(i >= 6)
					{
						nihia::setLed(result, g_padLeds[i],
							nihia::LedColor::Off, 0);
						continue;
					}
					switch(mmTrackColor(i))
					{
					case md::FrontPanel::LedColor::Green:
						color = nihia::LedColor::Green; lit = true; break;
					case md::FrontPanel::LedColor::Red:
						color = nihia::LedColor::Red; lit = true; break;
					case md::FrontPanel::LedColor::Yellow:
						color = nihia::LedColor::Yellow; lit = true; break;
					default:
						color = nihia::LedColor::Mint; break;
					}
				}
				else
				{
					lit = _mdPanel.getDrumLed(i);
					color = lit ? nihia::LedColor::Yellow
						: nihia::LedColor::Orange;
				}
			}
			else if((focused == md::MachineModel::Machinedrum
					? mdPatternBankHeld : mmPatternBankHeld) >= 0)
			{
				const auto heldBank = focused == md::MachineModel::Machinedrum
					? mdPatternBankHeld : mmPatternBankHeld;
				const auto selectedBank = focused == md::MachineModel::Machinedrum
					? mdSelectedPatternBank : mmSelectedPatternBank;
				const auto selectedSlot = focused == md::MachineModel::Machinedrum
					? mdSelectedPatternSlot : mmSelectedPatternSlot;
				const bool selected = selectedBank == heldBank
					&& selectedSlot == static_cast<int>(i);
				if(focused == md::MachineModel::Machinedrum && !selected
					&& (mdPatternBankOccupied & (1u << i)) == 0)
				{
					nihia::setLed(result, g_padLeds[i], nihia::LedColor::Off, 0);
					continue;
				}
				lit = selected;
				color = selected
					? (focused == md::MachineModel::Machinedrum
						? nihia::LedColor::Orange : nihia::LedColor::Green)
					: nihia::LedColor::White;
			}
			else if(focused == md::MachineModel::Monomachine)
			{
				const auto stepColor = monomachinePadColor(
					_mmPanel.getMonomachineStepLedColor(i), _mmRecording, _mmGridRecording);
				lit = stepColor != nihia::LedColor::Off;
				if(lit) color = stepColor;
			}
			else
			{
				const auto stepColor = machinedrumPadColor(_mdPanel, static_cast<unsigned>(i),
					_mdRecording, m_mdPlaying.load(), _mdGridRecording, _mdSelectedPage);
				lit = stepColor != nihia::LedColor::Off;
				if(lit) color = stepColor;
			}
			const bool patternBankHeld = (focused == md::MachineModel::Machinedrum
				? mdPatternBankHeld : mmPatternBankHeld) >= 0;
			if(pads[i] && !patternBankHeld
				&& (muteMode || padMode || color != nihia::LedColor::Red))
				color = nihia::LedColor::White;
			const auto intensity = muteMode
				? static_cast<uint8_t>(pads[i] || lit ? 3 : 2)
				: patternBankHeld
				? static_cast<uint8_t>(lit ? 3 : 1)
				: static_cast<uint8_t>(!padMode && lit && (!pads[i] || color == nihia::LedColor::Red)
					? stepPadBrightness(color)
					: pads[i] || lit ? 3 : padMode ? 2 : 1);
			nihia::setLed(result, g_padLeds[i], color, intensity);
			// Functional feedback always takes precedence, including the entire
			// mute/track/bank views (where an unlit pad also conveys information).
			if(randomLights && !muteMode && !padMode && !patternBankHeld
				&& !pads[i] && !lit)
				animateLight(g_padLeds[i], static_cast<uint8_t>(i), 16);
		}
		return result;
	}

	void Controller::run()
	{
		using namespace std::chrono_literals;
		std::shared_ptr<md::FrontPanelPublisher> mdPublisher;
		std::shared_ptr<md::FrontPanelPublisher> mmPublisher;
		md::FrontPanel mdPanel;
		md::FrontPanel mmPanel;
		auto previousLeft = std::make_unique<ScreenRenderer::Frame>();
		auto previousRight = std::make_unique<ScreenRenderer::Frame>();
		auto left = std::make_unique<ScreenRenderer::Frame>();
		auto right = std::make_unique<ScreenRenderer::Frame>();
		nihia::LedFrame previousLeds{};
		bool haveLeft = false;
		bool haveRight = false;
		bool haveLeds = false;
		// Opt-in, bounded hardware diagnostics; inactive during normal launches.
		const bool traceLeds = std::getenv("MD_MASCHINE_LED_TRACE") != nullptr;
		unsigned traceLines = 0;
		uint64_t mdTempoActivation = 0;
		uint64_t mmTempoActivation = 0;
		uint8_t mdBeat = 0;
		uint8_t mmBeat = 0;
		bool mdWasPlaying = false;
		bool mmWasPlaying = false;
		auto mdTempoPulseUntil = std::chrono::steady_clock::time_point{};
		auto mmTempoPulseUntil = std::chrono::steady_clock::time_point{};
		uint8_t mdTempoPulse = 0;
		uint8_t mmTempoPulse = 0;
		MachinedrumSectionDisplay mdDisplay;
		auto& mdSections = mdDisplay.sections;
		MonomachineSectionDisplay mmDisplay;
		auto& mmSections = mmDisplay.sections;
		auto lastLeftFrame = std::chrono::steady_clock::time_point{};
		auto lastRightFrame = std::chrono::steady_clock::time_point{};

		while(!m_stopping.load())
		{
			if(!m_client.isConnected())
			{
				if(!m_client.connect())
				{
					std::unique_lock waitLock(m_waitMutex);
					m_waitCondition.wait_for(waitLock, 2s,
						[this] { return m_stopping.load(); });
					continue;
				}
				haveLeft = false;
				haveRight = false;
				haveLeds = false;
			}

			if(!mdPublisher)
			{
				mdPublisher = m_machinedrum.tryGetFrontPanelPublisher();
				if(mdPublisher)
				{
					mdTempoActivation = mdPublisher->getLedActivationSequence(0x23, 5);
				}
			}
			if(!mmPublisher)
			{
				mmPublisher = m_monomachine.tryGetFrontPanelPublisher();
				if(mmPublisher)
				{
					mmTempoActivation = mmPublisher->getLedActivationSequence(0x26, 7);
				}
			}
			if(mdPublisher)
				(void)mdPublisher->tryRead(mdPanel);
			const bool mmPanelUpdated = mmPublisher && mmPublisher->tryRead(mmPanel);
			// The red half of each TRACK lamp marks the focused MM track.
			constexpr struct { uint8_t bank; uint8_t bit; } mmTrackLeds[] =
			{
				{0x25, 1}, {0x25, 3}, {0x24, 1},
				{0x24, 3}, {0x24, 5}, {0x24, 7},
			};
			if(mmPanelUpdated)
			{
				for(uint8_t track = 0; track < 6; ++track)
				{
					if((mmPanel.getLedBankRaw(mmTrackLeds[track].bank)
						& (1u << mmTrackLeds[track].bit)) == 0)
					{
						m_mmSelectedTrack.store(track);
						break;
					}
				}
			}

			const auto now = std::chrono::steady_clock::now();
			// RECORD lamps blink on the real units. Retain the mode across the
			// lamp's dark half, instead of clearing every playback section pulse.
			mdDisplay.update(mdPanel, m_mdPlaying.load(), now,
				m_mdRealtimeRecordGesture.exchange(false));
			const bool mdRecordActive = mdDisplay.recording.active;
			mmDisplay.update(mmPanel, m_mmPlaying.load(), now,
				m_mmRealtimeRecordGesture.exchange(false));
			const bool mmRecordActive = mmDisplay.recording.active;

			const auto nowMs = static_cast<uint64_t>(
				std::chrono::duration_cast<std::chrono::milliseconds>(
					now.time_since_epoch()).count());
			constexpr md::MachineModel displayModels[] =
			{
				md::MachineModel::Machinedrum, md::MachineModel::Machinedrum,
				md::MachineModel::Monomachine, md::MachineModel::Monomachine,
				md::MachineModel::Machinedrum, md::MachineModel::Monomachine,
			};
			constexpr md::PanelControl displayControls[] =
			{
				md::PanelControl::SynthesisEffectsRouting, md::PanelControl::Scale,
				md::PanelControl::DataPageForward, md::PanelControl::Scale,
				md::PanelControl::ClassicExtended, md::PanelControl::TrigSelect,
			};
			for(uint8_t slot = 0; slot < m_displayControlReleaseMs.size(); ++slot)
			{
				auto deadline = m_displayControlReleaseMs[slot].load();
				if(deadline != 0 && nowMs >= deadline
					&& m_displayControlReleaseMs[slot].compare_exchange_strong(
						deadline, 0))
					sendControl(displayModels[slot], displayControls[slot], false);
			}
			{
				std::lock_guard lock(m_inputMutex);
				if(m_mdPatternBankHeld >= 0)
				{
					const bool overlay = mdBankOverlayVisible(mdPanel);
					m_mdPatternBankScreenSeen |= overlay;
					// Bank-mode LEDs report occupied slots. Accumulate them so the
					// firmware's selected-slot blink cannot flash a Maschine pad off.
					if(overlay || (!m_mdPatternBankReleased
						&& nowMs - m_mdPatternBankPressedMs >= 100))
						for(unsigned i = 0; i < 16; ++i)
							if(mdPanel.getStepLed(i))
								m_mdPatternBankOccupied |= static_cast<uint16_t>(1u << i);
					if(m_mdPatternBankReleased && !overlay
						&& (m_mdPatternBankScreenSeen
							|| nowMs - m_mdPatternBankReleasedMs >= 250))
					{
						// The MD firmware has dismissed its temporary bank screen;
						// dismiss the matching pad preview in the same refresh.
						m_mdPatternBankHeld = -1;
						m_mdPatternBankReleased = false;
						m_mdPatternBankScreenSeen = false;
					}
				}
			}
			const auto updateTempoPulse = [&](const auto& _publisher,
				const uint8_t _command, const uint8_t _bit, const bool _playing,
				uint64_t& _lastActivation, uint8_t& _beat, bool& _wasPlaying,
				auto& _pulseUntil, uint8_t& _pulse)
			{
				if(_playing && !_wasPlaying)
					_beat = 0;
				else if(!_playing)
					_beat = 0;
				_wasPlaying = _playing;
				if(!_publisher)
					return;
				const auto activation = _publisher->getLedActivationSequence(
					_command, _bit);
				if(activation == 0 || activation == _lastActivation)
					return;
				_lastActivation = activation;
				if(!_playing)
					return;
				const bool downbeat = _beat == 0;
				_pulse = downbeat ? 3 : 2;
				_pulseUntil = now + (downbeat ? 160ms : 90ms);
				_beat = static_cast<uint8_t>((_beat + 1) & 3u);
			};
			updateTempoPulse(mdPublisher, 0x23, 5, m_mdPlaying.load(),
				mdTempoActivation, mdBeat, mdWasPlaying, mdTempoPulseUntil,
				mdTempoPulse);
			updateTempoPulse(mmPublisher, 0x26, 7, m_mmPlaying.load(),
				mmTempoActivation, mmBeat, mmWasPlaying, mmTempoPulseUntil,
				mmTempoPulse);
			if(now >= mdTempoPulseUntil)
				mdTempoPulse = 0;
			if(now >= mmTempoPulseUntil)
				mmTempoPulse = 0;

			const auto mdDimPages = mdDisplay.pulse;
			const auto mmDimPages = mmDisplay.pulse;

			const auto focused = m_focused.load();
			bool mixerHeld = false;
			{
				std::lock_guard lock(m_inputMutex);
				mixerHeld = m_mixerHeld;
			}
			ScreenRenderer::renderInto(*left, mdPanel,
				md::MachineModel::Machinedrum, focused, m_mdPlaying.load(), mixerHeld,
				mdSections.selected, mdDimPages, mdRecordActive, mdSections.enabled,
				m_machinedrum.getDrumHitMask());
			ScreenRenderer::renderInto(*right, mmPanel,
				md::MachineModel::Monomachine, focused, m_mmPlaying.load(), mixerHeld,
				mmSections.selected, mmDimPages, mmRecordActive, mmSections.enabled);
			const auto sendLeft = [&]
			{
				if((!haveLeft || now - lastLeftFrame >= 60ms)
					&& (!haveLeft || !ScreenRenderer::dirtyBounds(*previousLeft, *left).empty()))
				{
					if(m_client.sendFrame(0, *left))
					{
						previousLeft.swap(left);
						haveLeft = true;
						lastLeftFrame = now;
					}
				}
			};
			const auto sendRight = [&]
			{
				if((!haveRight || now - lastRightFrame >= 60ms)
					&& (!haveRight || !ScreenRenderer::dirtyBounds(*previousRight, *right).empty()))
				{
					if(m_client.sendFrame(1, *right))
					{
						previousRight.swap(right);
						haveRight = true;
						lastRightFrame = now;
					}
				}
			};
			if(focused == md::MachineModel::Monomachine)
			{
				sendRight();
				sendLeft();
			}
			else
			{
				sendLeft();
				sendRight();
			}
			const auto leds = buildLedFrame(mdPanel, mmPanel,
				mdTempoPulse, mmTempoPulse,
				mmRecordActive, mmRecordActive && !mmDisplay.recording.realtime,
				mdRecordActive, mdRecordActive && !mdDisplay.recording.realtime,
				static_cast<unsigned>(mdSections.selected));
			if(!haveLeds || leds != previousLeds)
			{
				if(m_client.sendLeds(leds))
				{
					if(traceLeds && traceLines++ < 6000)
					{
						uint16_t pressed = 0;
						int heldBank = -1;
						unsigned bankButtons = 0;
						{
							std::lock_guard lock(m_inputMutex);
							for(unsigned i = 0; i < m_padPressed.size(); ++i)
								if(m_padPressed[i]) pressed |= static_cast<uint16_t>(1u << i);
							for(unsigned i = 0; i < 8; ++i)
								if(m_buttonPressed[8 + i]) bankButtons |= 1u << i;
							heldBank = m_mmPatternBankHeld;
						}
						std::fprintf(stderr, "MASCHINE_LED t=%llu focus=%s mm27=%02x bank=%d buttons=%02x pressed=%04x raw=%02x,%02x,%02x,%02x pads=",
							static_cast<unsigned long long>(nowMs),
							focused == md::MachineModel::Monomachine ? "MM" : "MD",
							mmPanel.getLedBankRaw(0x27), heldBank, bankButtons, pressed,
							mmPanel.getLedBankRaw(0x20), mmPanel.getLedBankRaw(0x21),
							mmPanel.getLedBankRaw(0x22), mmPanel.getLedBankRaw(0x23));
						for(const auto led : g_padLeds)
							std::fprintf(stderr, "%02x,", leds[led - 1]);
						std::fprintf(stderr, "\n");
					}
					previousLeds = leds;
					haveLeds = true;
				}
			}

			std::unique_lock waitLock(m_waitMutex);
			m_waitCondition.wait_for(waitLock, 33ms,
				[this] { return m_stopping.load(); });
		}
		if(m_client.isConnected())
			(void)m_client.sendLeds({});
		m_client.disconnect();
	}
}
