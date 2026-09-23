#include "resamplerInOut.h"
#include "sampleRateTime.h"

#include <array>
#include <algorithm>
#include <cmath>

#include "dsp56kBase/logging.h"

#include <cstring>	// memset/memcpy

namespace synthLib
{
	ResamplerInOut::ResamplerInOut(uint32_t _channelCountIn, uint32_t _channelCountOut)
	: m_channelCountIn(_channelCountIn)
	, m_channelCountOut(_channelCountOut)
	, m_activeChannelCountOut(_channelCountOut)
	, m_scaledInput(_channelCountIn)
	, m_input(_channelCountIn)
	{
	}

	void ResamplerInOut::setDeviceSamplerate(float _samplerate)
	{
		if(m_samplerateDevice == _samplerate)
			return;

		m_samplerateDevice = _samplerate;
		recreate();
	}
	
	void ResamplerInOut::setHostSamplerate(float _samplerate)
	{
		if(m_samplerateHost == _samplerate)
			return;

		m_samplerateHost = _samplerate;
		recreate();
	}

	void ResamplerInOut::setSamplerates(const float _hostSamplerate, const float _deviceSamplerate)
	{
		if(m_samplerateDevice == _deviceSamplerate && m_samplerateHost == _hostSamplerate)
			return;

		m_samplerateDevice = _deviceSamplerate;
		m_samplerateHost = _hostSamplerate;

		recreate();
	}

	void ResamplerInOut::setActiveOutputChannelCount(const uint32_t _channelCount)
	{
		if(m_channelCountOut == 0)
			return;
		const auto count = std::clamp(_channelCount, 1u, m_channelCountOut);
		if(m_activeChannelCountOut == count)
			return;
		m_activeChannelCountOut = count;
		recreate();
	}

	void ResamplerInOut::reconfigure(const uint32_t _channelCountIn,
		const uint32_t _channelCountOut, const float _hostSamplerate,
		const float _deviceSamplerate)
	{
		const bool allOutputsWereActive = m_activeChannelCountOut == m_channelCountOut;
		m_channelCountIn = _channelCountIn;
		m_channelCountOut = _channelCountOut;
		if(allOutputsWereActive || m_activeChannelCountOut == 0
			|| m_activeChannelCountOut > m_channelCountOut)
			m_activeChannelCountOut = m_channelCountOut;
		m_samplerateHost = _hostSamplerate;
		m_samplerateDevice = _deviceSamplerate;
		recreate();
	}

	void ResamplerInOut::reserveMidiEventCapacity(const size_t _capacity)
	{
		m_processedMidiIn.reserve(_capacity);
		m_midiIn.reserve(_capacity);
		m_midiOut.reserve(_capacity);
		m_pendingMidiOut.reserve(_capacity);
	}

	void ResamplerInOut::prepare(const uint32_t _maxHostBlockSize)
	{
		m_preparedHostBlockSize = _maxHostBlockSize;
		if(!m_in || !m_out || m_samplerateHost < 1.0f || m_samplerateDevice < 1.0f)
			return;

		const auto maxDeviceBlock = static_cast<uint32_t>(std::ceil(
			static_cast<double>(_maxHostBlockSize) * m_samplerateDevice
			/ m_samplerateHost)) + 1024;
		m_input.reserve(static_cast<size_t>(_maxHostBlockSize) * 2 + 1024);
		m_scaledInput.reserve(static_cast<size_t>(maxDeviceBlock) * 2 + 1024);
		m_out->prepare(m_activeChannelCountOut, _maxHostBlockSize);
		m_in->prepare(m_channelCountIn, maxDeviceBlock);
		for(size_t channel = m_activeChannelCountOut; channel < m_channelCountOut; ++channel)
			m_nativeOutputScratch[channel].reserve(maxDeviceBlock);
	}

	void ResamplerInOut::recreate()
	{
		m_out.reset();
		m_in.reset();
		m_scaledInput = AudioBuffer(m_channelCountIn);
		m_input = AudioBuffer(m_channelCountIn);
		m_processedMidiIn.clear();
		m_midiIn.clear();
		m_midiOut.clear();
		m_pendingMidiOut.clear();
		m_hostSamples = 0;
		m_deviceSamples = 0;
		m_scaledInputSize = 0;
		m_inputLatency = 0;
		m_outputLatency = 0;
		m_inputPadding = 0;

		if(m_samplerateDevice < 1 || m_samplerateHost < 1)
			return;

		m_out.reset(new Resampler(m_samplerateDevice, m_samplerateHost));
		m_in.reset(new Resampler(m_samplerateHost, m_samplerateDevice));

		// prewarm to calculate latency
		std::array<std::vector<float>, 12> data;

		TAudioInputs ins;
		TAudioOutputs outs;

		for(size_t i=0; i<data.size(); ++i)
			data[i].resize(512, 0);

		for(size_t i=0; i<ins.size(); ++i)
			ins[i] = i >= data.size() ? nullptr : &data[i][0];

		for(size_t i=0; i<outs.size(); ++i)
			outs[i] = i >= data.size() ? nullptr : &data[i][0];

		TMidiVec midiIn, midiOut;
		process(ins, outs, TMidiVec(), midiOut,
			static_cast<uint32_t>(data[0].size()),
			[&](const TAudioInputs&, const TAudioOutputs&, size_t, const TMidiVec&, TMidiVec&)
		{
		});
		if(m_samplerateDevice != m_samplerateHost)
		{
			const auto ratio = static_cast<double>(m_samplerateHost) / m_samplerateDevice;
			if(m_channelCountIn)
			{
				// Nested source-range rounding can require one more native sample
				// at a later block boundary. Reserve its host equivalent now, so
				// changing block sizes never inserts silence into live input.
				const auto guard = static_cast<uint32_t>(std::ceil(ratio));
				m_input.insertZeroes(guard);
				m_inputPadding += guard;
			}
			const auto outputFilter = m_out->getGroupDelay() * ratio;
			const auto outputDelay = static_cast<double>(m_deviceSamples) * ratio
				- data[0].size() + outputFilter;
			m_outputLatency = static_cast<uint32_t>(std::ceil(std::max(0.0, outputDelay)));
			const auto roundTrip = static_cast<uint32_t>(std::ceil(
				m_inputPadding + m_in->getGroupDelay() + outputFilter));
			m_inputLatency = roundTrip > m_outputLatency ? roundTrip - m_outputLatency : 0;
		}
		// The dummy warmup does not advance the device. Start both event
		// timelines at the first real callback, retaining fractional rate phase
		// by converting absolute positions instead of rounding each block.
		m_hostSamples = 0;
		m_deviceSamples = 0;
		if(m_preparedHostBlockSize)
			prepare(m_preparedHostBlockSize);
	}

	void ResamplerInOut::process(const TAudioInputs& _inputs, TAudioOutputs& _outputs,
		const TMidiVec& _midiIn, TMidiVec& _midiOut, const uint32_t _numSamples,
		const TProcessFunc& _processFunc)
	{
		if(!m_in || !m_out)
			return;

		if(m_samplerateDevice == m_samplerateHost)
		{
			_processFunc(_inputs, _outputs, _numSamples, _midiIn, _midiOut);
			return;
		}

		for(const auto& event : _midiIn)
			m_midiIn.push_back({event, rescaleSamplesCeil(m_hostSamples + event.offset,
				m_samplerateHost, m_samplerateDevice)});

		m_input.append(_inputs, _numSamples);

		auto feedInput = [&](TAudioOutputs& _data, uint32_t _numRequestedSamples)
		{
			const auto offset = _numRequestedSamples > m_input.size() ? _numRequestedSamples - m_input.size() : 0;
			if(offset)
			{
				// resampler prewarming, wants more data than we have
				for(size_t c=0; c<m_channelCountIn; ++c)
				{
					memset(_data[c], 0, sizeof(float) * offset);
					_data[c] += offset;
				}
			}

			const auto count = (_numRequestedSamples - offset);

			if(count)
			{
				for(size_t c=0; c<m_channelCountIn; ++c)
					memcpy(_data[c], &m_input.getChannel(c)[0], sizeof(float) * count);

				m_input.remove(count);
			}

			m_inputPadding += static_cast<uint32_t>(offset);
			if(offset)
			{
				LOG_DIAGNOSTIC("Resampler input padding " << m_inputPadding << " samples");
			}
		};

		auto feedOutput = [&](const TAudioOutputs& _outs, const uint32_t _numProcessedSamples)
		{
			if(m_channelCountIn)
			{
				// A one-sample host callback can still request a native sample
				// when its rounded size estimate is zero. Size the scratch buffer
				// from the actual pull before exposing writable channel pointers.
				m_scaledInput.ensureSize(m_scaledInputSize + _numProcessedSamples);
				m_scaledInputSize += m_in->process(m_scaledInput, m_scaledInputSize, m_channelCountIn, _numProcessedSamples, false, feedInput);
			}

			m_processedMidiIn.clear();
			const auto end = m_deviceSamples + _numProcessedSamples;
			m_midiIn.erase(std::remove_if(m_midiIn.begin(), m_midiIn.end(), [&](const TimedMidiEvent& timed)
			{
				if(timed.sample >= end)
					return false;
				m_processedMidiIn.push_back(timed.event);
				m_processedMidiIn.back().offset = static_cast<uint32_t>(
					timed.sample > m_deviceSamples ? timed.sample - m_deviceSamples : 0);
				return true;
			}), m_midiIn.end());

			TAudioInputs inputs;

			if(m_channelCountIn)
			{
				if(_numProcessedSamples > m_scaledInputSize)
				{
					// resampler prewarming, wants more data than we have
					const auto diff = _numProcessedSamples - m_scaledInputSize;
					m_scaledInput.insertZeroes(diff);
					m_scaledInputSize += diff;
					m_outputLatency += static_cast<uint32_t>(diff);
					LOG_DIAGNOSTIC("Resampler output latency " << m_outputLatency << " samples");
				}
				m_scaledInput.fillPointers(inputs);
			}
			else
			{
				inputs.fill(nullptr);
			}

			TAudioOutputs deviceOutputs = _outs;
			for(size_t channel = m_activeChannelCountOut; channel < m_channelCountOut; ++channel)
			{
				auto& scratch = m_nativeOutputScratch[channel];
				scratch.resize(_numProcessedSamples);
				deviceOutputs[channel] = scratch.data();
			}
			_processFunc(inputs, deviceOutputs, _numProcessedSamples, m_processedMidiIn, m_midiOut);
			for(const auto& event : m_midiOut)
				m_pendingMidiOut.push_back({event, rescaleSamplesCeil(m_deviceSamples + event.offset,
					m_samplerateDevice, m_samplerateHost) + m_outputLatency});
			m_midiOut.clear();
			m_deviceSamples = end;

			if(m_channelCountIn)
			{
				m_scaledInput.remove(_numProcessedSamples);
				m_scaledInputSize -= _numProcessedSamples;
			}
		};

		m_out->process(_outputs, m_activeChannelCountOut,
			_numSamples, false, feedOutput);

		const auto end = m_hostSamples + _numSamples;
		m_pendingMidiOut.erase(std::remove_if(m_pendingMidiOut.begin(), m_pendingMidiOut.end(), [&](const TimedMidiEvent& timed)
		{
			if(timed.sample >= end)
				return false;
			_midiOut.push_back(timed.event);
			_midiOut.back().offset = static_cast<uint32_t>(
				timed.sample > m_hostSamples ? timed.sample - m_hostSamples : 0);
			return true;
		}), m_pendingMidiOut.end());
		m_hostSamples = end;
	}
}
