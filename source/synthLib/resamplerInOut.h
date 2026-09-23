#pragma once

#include "audiobuffer.h"
#include "functionRef.h"
#include "midiTypes.h"
#include "resampler.h"

#include <array>
#include <memory>	// unique_ptr
#include <tuple>
#include <vector>

namespace synthLib
{
	class ResamplerInOut
	{
	public:
		using TMidiVec = std::vector<SMidiEvent>;
		using TProcessFunc = FunctionRef<void(const TAudioInputs&, const TAudioOutputs&,
			size_t, const TMidiVec&, TMidiVec&)>;

		ResamplerInOut(uint32_t _channelCountIn, uint32_t _channelCountOut);

		void setDeviceSamplerate(float _samplerate);
		void setHostSamplerate(float _samplerate);
		void setSamplerates(float _hostSamplerate, float _deviceSamplerate);
		void setActiveOutputChannelCount(uint32_t _channelCount);
		void reconfigure(uint32_t _channelCountIn, uint32_t _channelCountOut,
			float _hostSamplerate, float _deviceSamplerate);
		void reserveMidiEventCapacity(size_t _capacity);
		void prepare(uint32_t _maxHostBlockSize);

		void process(const TAudioInputs& _inputs, TAudioOutputs& _outputs,
			const TMidiVec& _midiIn, TMidiVec& _midiOut, uint32_t _numSamples,
			const TProcessFunc& _processFunc);

		uint32_t getOutputLatency() const { return m_outputLatency; }
		uint32_t getInputLatency() const { return m_inputLatency; }
		uint32_t getInputPaddingSamples() const { return m_inputPadding; }

	private:
		void recreate();
		struct TimedMidiEvent
		{
			SMidiEvent event;
			uint64_t sample;
		};

		uint32_t m_channelCountIn;
		uint32_t m_channelCountOut;
		uint32_t m_activeChannelCountOut;

		std::unique_ptr<Resampler> m_out = nullptr;
		std::unique_ptr<Resampler> m_in = nullptr;

		float m_samplerateDevice = 0;
		float m_samplerateHost = 0;
		AudioBuffer m_scaledInput;
		AudioBuffer m_input;
		std::array<std::vector<float>, std::tuple_size_v<TAudioOutputs>> m_nativeOutputScratch;

		size_t m_scaledInputSize = 0;

		TMidiVec m_processedMidiIn;

		std::vector<TimedMidiEvent> m_midiIn;
		std::vector<TimedMidiEvent> m_pendingMidiOut;
		TMidiVec m_midiOut;
		uint64_t m_hostSamples = 0;
		uint64_t m_deviceSamples = 0;

		uint32_t m_inputLatency = 0;
		uint32_t m_outputLatency = 0;
		uint32_t m_inputPadding = 0;
		uint32_t m_preparedHostBlockSize = 0;
	};
}
