#include "plugin.h"
#include "sampleRateTime.h"
#include "device.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>

#include "baseLib/os.h"

using namespace synthLib;

namespace
{
	uint64_t nowNanoseconds() noexcept
	{
		return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
			std::chrono::steady_clock::now().time_since_epoch()).count());
	}
}

namespace synthLib
{
	constexpr uint8_t g_stateVersion = 1;

	Plugin::Plugin(Device* _device, CallbackDeviceInvalid _callbackDeviceInvalid)
	: m_resampler(_device->getChannelCountIn(), _device->getChannelCountOut())
	, m_device(_device)
	, m_midiClock(*this)
	, m_extraLatencyBlocks(_device->getDefaultLatencyBlocks())
	, m_deviceSamplerate(_device->getSamplerate())
	, m_callbackDeviceInvalid(std::move(_callbackDeviceInvalid))
	{
	}

	void Plugin::addMidiEvent(const SMidiEvent& _ev)
	{
		std::lock_guard lock(m_lockAddMidiEvent);

		if(m_midiInRingBuffer.full())
		{
			std::lock_guard l(m_lock);
			processMidiInEvent(m_midiInRingBuffer.pop_front(), false);
		}
		m_midiInRingBuffer.push_back(_ev);
	}

	bool Plugin::setPreferredDeviceSamplerate(const float _samplerate)
	{
		std::lock_guard lock(m_lock);

		const auto sr = m_device->getDeviceSamplerate(_samplerate, m_hostSamplerate);

		if(sr == m_deviceSamplerate)  // NOLINT(clang-diagnostic-float-equal)
			return true;

		if(!m_device->setSamplerate(sr))
			return false;

		m_deviceSamplerate = sr;
		m_resampler.setSamplerates(m_hostSamplerate, m_deviceSamplerate);

		updateDeviceLatency();
		return true;
	}

	void Plugin::setHostSamplerate(const float _hostSamplerate, const float _preferredDeviceSamplerate)
	{
		std::lock_guard lock(m_lock);

		m_deviceSamplerate = m_device->getDeviceSamplerate(_preferredDeviceSamplerate, _hostSamplerate);
		m_device->setSamplerate(m_deviceSamplerate);
		m_resampler.setSamplerates(_hostSamplerate, m_deviceSamplerate);

		m_hostSamplerate = _hostSamplerate;
		m_hostSamplerateInv = _hostSamplerate > 0 ? 1.0f / _hostSamplerate : 0.0f;

		updateDeviceLatency();
	}

	void Plugin::reserveMidiEventCapacity(const size_t _capacity)
	{
		std::lock_guard lock(m_lock);
		m_midiIn.reserve(_capacity);
		m_midiOut.reserve(_capacity);
		m_resampler.reserveMidiEventCapacity(_capacity);
		m_device->reserveMidiEventCapacity(_capacity);
	}

	void Plugin::process(const TAudioInputs& _inputs, const TAudioOutputs& _outputs,
		const size_t _count, const double _bpm, const double _ppqPos,
		const bool _isPlaying, const bool _ppqKnown)
	{
		baseLib::setFlushDenormalsToZero();
		const auto instrument = m_realtimeInstrumentation.isEnabled();
		const auto processStart = instrument ? nowNanoseconds() : 0;

		TAudioInputs inputs(_inputs);
		TAudioOutputs outputs(_outputs);

		std::unique_lock lock(m_lock, std::defer_lock);
		if(instrument)
		{
			const auto lockStart = nowNanoseconds();
			lock.lock();
			m_realtimeInstrumentation.recordSynthProcessLockWait(
				nowNanoseconds() - lockStart);
		}
		else
		{
			lock.lock();
		}
		if(_count > m_blockSize)
			++m_realtimeAllocationFallbackCount;

		auto* const silentInput = getSilentInputBuffer(_count);
		for(size_t i=0; i<inputs.size(); ++i)
			inputs[i] = _inputs[i] ? _inputs[i] : silentInput;

		if(!m_device->isValid())
		{
			m_callbackDeviceInvalid(m_device);
			if(instrument)
				m_realtimeInstrumentation.recordSynthProcess(
					nowNanoseconds() - processStart);
			return;
		}

		for(size_t i=0; i<m_device->getChannelCountOut(); ++i)
			outputs[i] = _outputs[i] ? _outputs[i]
				: getDiscardOutputBuffer(i, _count);

		processMidiInEvents();
		processMidiClock(_bpm, _ppqPos, _isPlaying, _count, _ppqKnown);

		if(instrument)
			RealtimeInstrumentation::setCurrentDeviceContext(static_cast<uint32_t>(m_deviceSamplerate),
				0, m_device->getDspClockPercent());
		const auto midiOutBegin = m_midiOut.size();
		const auto resamplerStart = instrument ? nowNanoseconds() : 0;
		uint64_t deviceProcessNanoseconds = 0;
		m_resampler.process(inputs, outputs, m_midiIn, m_midiOut,
			static_cast<uint32_t>(_count),
			[&](const TAudioInputs& _ins, const TAudioOutputs& _outs, size_t _c, const ResamplerInOut::TMidiVec& _midiIn, ResamplerInOut::TMidiVec& _midiOut)
			{
				const auto deviceStart = instrument ? nowNanoseconds() : 0;
				m_device->process(_ins, _outs, _c, _midiIn, _midiOut);
				if(instrument)
					deviceProcessNanoseconds += nowNanoseconds() - deviceStart;
			});
		if(instrument)
		{
			m_realtimeInstrumentation.recordResampler(
				nowNanoseconds() - resamplerStart, deviceProcessNanoseconds, _count,
				m_hostSamplerate != m_deviceSamplerate); // NOLINT(clang-diagnostic-float-equal)
		}
		for(size_t i = midiOutBegin; i < m_midiOut.size(); ++i)
			if(!m_midiOut[i].sysex.empty())
				++m_realtimeAllocationFallbackCount;

		m_midiIn.clear();
		if(instrument)
			m_realtimeInstrumentation.recordSynthProcess(
				nowNanoseconds() - processStart);
	}

	void Plugin::getMidiOut(std::vector<SMidiEvent>& _midiOut)
	{
		std::swap(_midiOut, m_midiOut);
		m_midiOut.clear();
	}

	bool Plugin::isValid() const
	{
		return m_device->isValid();
	}

	void Plugin::setDevice(Device* _device)
	{
		if(!_device)
			return;

		std::lock_guard lock(m_lock);

		std::vector<uint8_t> deviceState;
		getState(deviceState, StateTypeGlobal);

		delete m_device;

		m_device = _device;

		configureDeviceAudio();
		if(!deviceState.empty())
			setState(deviceState);

		// MIDI clock has to send the start event again, some device find it confusing and do strange things if there isn't any
		m_midiClock.restart();

	}

#if !SYNTHLIB_DEMO_MODE
	bool Plugin::getState(std::vector<uint8_t>& _state, StateType _type) const
	{
		std::lock_guard lock(m_lock);

		if(!m_device)
			return false;

		_state.push_back(g_stateVersion);
		_state.push_back(_type);

		return m_device->getState(_state, _type);
	}

	bool Plugin::setState(const std::vector<uint8_t>& _state) const
	{
		if(_state.empty())
			return false;

		if(_state.size() < 2)
		{
			std::lock_guard lock(m_lock);
			return m_device && m_device->setStateFromUnknownCustomData(_state);
		}

		const auto version = _state[0];

		if(version != g_stateVersion)
		{
			std::lock_guard lock(m_lock);
			return m_device && m_device->setStateFromUnknownCustomData(_state);
		}

		const auto stateType = static_cast<StateType>(_state[1]);

		auto state = _state;
		state.erase(state.begin(), state.begin() + 2);
		auto transactionState =
			std::make_shared<const std::vector<uint8_t>>(std::move(state));

		Device* transactionDevice = nullptr;
		std::unique_ptr<Device::StateTransaction> transaction;
		{
			std::lock_guard lock(m_lock);
			if(!m_device)
				return false;
			if(!m_device->supportsStateTransactions())
				return m_device->setState(*transactionState, stateType);
			transactionDevice = m_device;
			transaction = m_device->beginStateTransaction(
				std::move(transactionState), stateType);
		}
		if(!transaction)
			return false;

		const auto preparationSucceeded = transaction->prepare();
		bool finished = false;
		{
			std::lock_guard lock(m_lock);
			if(m_device == transactionDevice)
				finished = m_device->finishStateTransaction(*transaction);
		}
		// The transaction may own a replaced Device implementation. Its destructor
		// deliberately runs after the process/device lock has been released.
		return preparationSucceeded && finished;
	}
#endif
	void Plugin::insertMidiEvent(const SMidiEvent& _ev)
	{
		if(m_midiIn.empty() || m_midiIn.back().offset <= _ev.offset)
		{
			m_midiIn.push_back(_ev);
			return;
		}

		for (auto it = m_midiIn.begin(); it != m_midiIn.end(); ++it)
		{
			if (it->offset > _ev.offset)
			{
				m_midiIn.insert(it, _ev);
				return;
			}
		}

		m_midiIn.push_back(_ev);
	}

	bool Plugin::setLatencyBlocks(uint32_t _latencyBlocks)
	{
		std::lock_guard lock(m_lock);

		if(m_extraLatencyBlocks == _latencyBlocks)
			return false;

		m_extraLatencyBlocks = _latencyBlocks;
		updateDeviceLatency();
		return true;
	}

	void Plugin::processMidiClock(const double _bpm, const double _ppqPos, const bool _isPlaying, const size_t _sampleCount, const bool _ppqKnown)
	{
		m_midiClock.process(_bpm, _ppqPos, _isPlaying, _sampleCount, _ppqKnown);
	}

	float* Plugin::getSilentInputBuffer(const size_t _minimumSize)
	{
		if(m_silentInputBuffer.size() < _minimumSize)
			m_silentInputBuffer.resize(_minimumSize);
		std::fill_n(m_silentInputBuffer.data(), _minimumSize, 0.0f);
		return m_silentInputBuffer.data();
	}

	float* Plugin::getDiscardOutputBuffer(const size_t _channel,
		const size_t _minimumSize)
	{
		auto& buffer = m_discardOutputBuffers[_channel];
		if(buffer.size() < _minimumSize)
			buffer.resize(_minimumSize);
		return buffer.data();
	}

	void Plugin::configureDeviceAudio()
	{
		m_device->setSamplerate(m_deviceSamplerate);
		m_resampler.reconfigure(m_device->getChannelCountIn(),
			m_device->getChannelCountOut(), m_hostSamplerate, m_deviceSamplerate);
		for(size_t channel = 0; channel < m_device->getChannelCountOut(); ++channel)
			m_discardOutputBuffers[channel].resize(m_blockSize);
		updateDeviceLatency();
	}

	void Plugin::updateDeviceLatency()
	{
		if(m_blockSize <= 0 || m_hostSamplerate <= 0)
			return;

		const auto latency = rescaleSamplesCeil(uint64_t(m_blockSize) * m_extraLatencyBlocks,
			m_hostSamplerate, m_device->getSamplerate());
		m_device->setExtraLatencySamples(static_cast<uint32_t>(std::min<uint64_t>(latency,
			std::numeric_limits<uint32_t>::max())));
		m_deviceExtraLatencyHost = static_cast<uint32_t>(rescaleSamplesCeil(
			m_device->getExtraLatencySamples(), m_device->getSamplerate(), m_hostSamplerate));

		m_deviceLatencyMidiToOutput = static_cast<uint32_t>(rescaleSamplesCeil(
			m_device->getInternalLatencyMidiToOutput(), m_device->getSamplerate(), m_hostSamplerate));
		m_deviceLatencyInputToOutput = static_cast<uint32_t>(rescaleSamplesCeil(
			m_device->getInternalLatencyInputToOutput(), m_device->getSamplerate(), m_hostSamplerate));
	}

	void Plugin::processMidiInEvents()
	{
		while (!m_midiInRingBuffer.empty())
		{
			const auto ev = m_midiInRingBuffer.pop_front();

			processMidiInEvent(ev, true);
		}
	}

	void Plugin::processMidiInEvent(const SMidiEvent& _ev, const bool _realtime)
	{
		// sysex might be sent in multiple chunks. Happens if coming from hardware
		if (!_ev.sysex.empty())
		{
			if(_realtime)
				++m_realtimeAllocationFallbackCount;
			const bool isComplete = _ev.sysex.front() == M_STARTOFSYSEX && _ev.sysex.back() == M_ENDOFSYSEX;

			if (isComplete)
			{
				m_midiIn.push_back(_ev);
				return;
			}

			const bool isStart = _ev.sysex.front() == M_STARTOFSYSEX && _ev.sysex.back() != M_ENDOFSYSEX;
			const bool isEnd = _ev.sysex.front() != M_STARTOFSYSEX && _ev.sysex.back() == M_ENDOFSYSEX;

			if (isStart)
			{
				m_pendingSysexInput = _ev;
				return;
			}

			if (!m_pendingSysexInput.sysex.empty())
			{
				m_pendingSysexInput.sysex.insert(m_pendingSysexInput.sysex.end(), _ev.sysex.begin(), _ev.sysex.end());

				if (isEnd)
				{
					m_midiIn.push_back(m_pendingSysexInput);
					m_pendingSysexInput.sysex.clear();
				}
			}
		}

		m_midiIn.push_back(_ev);
	}

	void Plugin::setBlockSize(const uint32_t _blockSize)
	{
		std::lock_guard lock(m_lock);
		m_blockSize = _blockSize;
		m_silentInputBuffer.resize(_blockSize);
		for(size_t channel = 0; channel < m_device->getChannelCountOut(); ++channel)
			m_discardOutputBuffers[channel].resize(_blockSize);
		m_resampler.prepare(_blockSize);
		updateDeviceLatency();
	}

	void Plugin::setActiveOutputChannelCount(const uint32_t _channelCount)
	{
		std::lock_guard lock(m_lock);
		m_resampler.setActiveOutputChannelCount(std::min(_channelCount, m_device->getChannelCountOut()));
		updateDeviceLatency();
	}

	uint32_t Plugin::getLatencyMidiToOutput() const
	{
		std::lock_guard lock(m_lock);
		return m_deviceExtraLatencyHost + m_deviceLatencyMidiToOutput
			+ m_resampler.getOutputLatency();
	}

	uint32_t Plugin::getLatencyInputToOutput() const
	{
		std::lock_guard lock(m_lock);
		return m_deviceExtraLatencyHost + m_deviceLatencyInputToOutput
			+ m_resampler.getOutputLatency() + m_resampler.getInputLatency();
	}
}
