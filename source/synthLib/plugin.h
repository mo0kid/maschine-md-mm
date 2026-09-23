#pragma once

#include <array>
#include <mutex>
#include <atomic>
#include <functional>
#include <optional>
#include <type_traits>
#include <tuple>
#include <utility>

#include "midiTypes.h"
#include "realtimeInstrumentation.h"
#include "resamplerInOut.h"
#include "buildconfig.h"

#include "dsp56kBase/ringbuffer.h"

#include "deviceTypes.h"
#include "midiClock.h"

namespace synthLib
{
	class Device;

	class Plugin
	{
	public:
		static constexpr size_t RealtimeMidiEventCapacity = 1024;

		using CallbackDeviceInvalid = std::function<void(Device*)>;

		Plugin(Device* _device, CallbackDeviceInvalid _callbackDeviceInvalid);

		void addMidiEvent(const SMidiEvent& _ev);

		bool setPreferredDeviceSamplerate(float _samplerate);

		void setHostSamplerate(float _hostSamplerate, float _preferredDeviceSamplerate);
		float getHostSamplerate() const { return m_hostSamplerate; }
		float getHostSamplerateInv() const { return m_hostSamplerateInv; }

		void setBlockSize(uint32_t _blockSize);
		void setActiveOutputChannelCount(uint32_t _channelCount);
		void reserveMidiEventCapacity(size_t _capacity = RealtimeMidiEventCapacity);

		uint32_t getLatencyMidiToOutput() const;
		uint32_t getLatencyInputToOutput() const;
		// Counts visits to explicitly unsupported RT allocation paths: a host block
		// larger than setBlockSize, or dynamically sized SysEx processed/emitted by
		// the device. Prepared audio and channel-message MIDI must leave this stable.
		uint64_t getRealtimeAllocationFallbackCount() const
		{
			return m_realtimeAllocationFallbackCount.load();
		}
		RealtimeInstrumentation& getRealtimeInstrumentation() noexcept
		{
			return m_realtimeInstrumentation;
		}
		const RealtimeInstrumentation& getRealtimeInstrumentation() const noexcept
		{
			return m_realtimeInstrumentation;
		}

		void process(const TAudioInputs& _inputs, const TAudioOutputs& _outputs,
			size_t _count, double _bpm, double _ppqPos, bool _isPlaying, bool _ppqKnown = true);
		void getMidiOut(std::vector<SMidiEvent>& _midiOut);

		bool isValid() const;

		void setDevice(Device* _device);
		Device* getDevice() const { return m_device; }

		// Keep a short control-plane operation pinned to the current Device. Callers
		// must not perform file I/O, allocation-heavy preparation, or other long work
		// inside this callback; capture immutable context here and prepare outside.
		template<typename Callback>
		decltype(auto) withDeviceLocked(Callback&& _callback) const
		{
			std::lock_guard lock(m_lock);
			return std::forward<Callback>(_callback)(m_device);
		}

		// UI/status polling must never wait behind realtime processing. Returns an
		// empty optional when the audio thread currently owns the device lock.
		template<typename Callback, typename Result = std::invoke_result_t<Callback, Device*>>
		std::optional<Result> tryWithDeviceLocked(Callback&& _callback) const
		{
			static_assert(!std::is_void_v<Result>);
			std::unique_lock lock(m_lock, std::try_to_lock);
			if(!lock.owns_lock())
				return std::nullopt;
			return std::optional<Result>{std::in_place,
				std::forward<Callback>(_callback)(m_device)};
		}

#if !SYNTHLIB_DEMO_MODE
		bool getState(std::vector<uint8_t>& _state, StateType _type) const;
		bool setState(const std::vector<uint8_t>& _state) const;
#endif
		void insertMidiEvent(const SMidiEvent& _ev);

		bool setLatencyBlocks(uint32_t _latencyBlocks);
		uint32_t getLatencyBlocks() const { return m_extraLatencyBlocks; }

	private:
		void processMidiClock(double _bpm, double _ppqPos, bool _isPlaying, size_t _sampleCount, bool _ppqKnown);
		float* getSilentInputBuffer(size_t _minimumSize);
		float* getDiscardOutputBuffer(size_t _channel, size_t _minimumSize);
		void configureDeviceAudio();
		void updateDeviceLatency();
		void processMidiInEvents();
		void processMidiInEvent(const SMidiEvent& _ev, bool _realtime);

		dsp56k::RingBuffer<SMidiEvent, RealtimeMidiEventCapacity, false> m_midiInRingBuffer;
		std::vector<SMidiEvent> m_midiIn;
		std::vector<SMidiEvent> m_midiOut;

		SMidiEvent m_pendingSysexInput;

		ResamplerInOut m_resampler;
		mutable std::recursive_mutex m_lock;
		mutable std::mutex m_lockAddMidiEvent;

		Device* m_device;

		std::vector<float> m_silentInputBuffer;
		std::array<std::vector<float>, std::tuple_size_v<TAudioOutputs>>
			m_discardOutputBuffers;

		float m_hostSamplerate = 0.0f;
		float m_hostSamplerateInv = 0.0f;

		uint32_t m_blockSize = 0;

		uint32_t m_deviceLatencyMidiToOutput = 0;
		uint32_t m_deviceLatencyInputToOutput = 0;
		uint32_t m_deviceExtraLatencyHost = 0;

		MidiClock m_midiClock;

		uint32_t m_extraLatencyBlocks = 1;

		float m_deviceSamplerate = 0.0f;
		CallbackDeviceInvalid m_callbackDeviceInvalid;
		std::atomic<uint64_t> m_realtimeAllocationFallbackCount{0};
		RealtimeInstrumentation m_realtimeInstrumentation;
	};
}
