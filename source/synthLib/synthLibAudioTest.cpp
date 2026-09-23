#include "device.h"
#include "plugin.h"
#include "syntheticAudioTestDevice.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>
#include <new>
#include <stdexcept>
#include <thread>

#if defined(_MSC_VER)
#include <malloc.h>
#endif

namespace
{
	thread_local bool g_detectAllocations = false;
	thread_local size_t g_detectedAllocationCount = 0;
}

void* operator new(const std::size_t _size)
{
	if(g_detectAllocations)
		++g_detectedAllocationCount;
	if(void* const result = std::malloc(_size))
		return result;
	throw std::bad_alloc();
}

void* operator new[](const std::size_t _size)
{
	return ::operator new(_size);
}

void* operator new(const std::size_t _size, const std::align_val_t _alignment)
{
	if(g_detectAllocations)
		++g_detectedAllocationCount;
	void* result = nullptr;
#if defined(_MSC_VER)
	result = _aligned_malloc(_size, static_cast<std::size_t>(_alignment));
#else
	if(posix_memalign(&result, static_cast<std::size_t>(_alignment), _size) != 0)
		result = nullptr;
#endif
	if(result)
		return result;
	throw std::bad_alloc();
}

void* operator new[](const std::size_t _size, const std::align_val_t _alignment)
{
	return ::operator new(_size, _alignment);
}

void operator delete(void* const _pointer) noexcept
{
	std::free(_pointer);
}

void operator delete[](void* const _pointer) noexcept
{
	std::free(_pointer);
}

void operator delete(void* const _pointer, std::size_t) noexcept
{
	std::free(_pointer);
}

void operator delete[](void* const _pointer, std::size_t) noexcept
{
	std::free(_pointer);
}

void operator delete(void* const _pointer, std::align_val_t) noexcept
{
#if defined(_MSC_VER)
	_aligned_free(_pointer);
#else
	std::free(_pointer);
#endif
}

void operator delete[](void* const _pointer, const std::align_val_t _alignment) noexcept
{
	::operator delete(_pointer, _alignment);
}

void operator delete(void* const _pointer, std::size_t,
	const std::align_val_t _alignment) noexcept
{
	::operator delete(_pointer, _alignment);
}

void operator delete[](void* const _pointer, std::size_t,
	const std::align_val_t _alignment) noexcept
{
	::operator delete(_pointer, _alignment);
}

namespace
{
	void require(const bool _condition, const char* const _message)
	{
		if(!_condition)
			throw std::runtime_error(_message);
	}

	using SyntheticAudioDevice = synthLib::test::SyntheticAudioDevice;

	class MissingInputProbeDevice final : public synthLib::Device
	{
	public:
		MissingInputProbeDevice() : Device({}) {}

		float getSamplerate() const override { return 44100.0f; }
		bool isValid() const override { return true; }
#if SYNTHLIB_DEMO_MODE == 0
		bool getState(std::vector<uint8_t>&, synthLib::StateType) override
		{
			return false;
		}
		bool setState(const std::vector<uint8_t>&, synthLib::StateType) override
		{
			return false;
		}
#endif
		uint32_t getChannelCountIn() override { return 2; }
		uint32_t getChannelCountOut() override { return 6; }
		bool setDspClockPercent(uint32_t) override { return false; }
		uint32_t getDspClockPercent() const override { return 100; }
		uint64_t getDspClockHz() const override { return 100000000; }
		bool receivedOnlySilence() const { return m_receivedOnlySilence; }

	protected:
		void readMidiOut(std::vector<synthLib::SMidiEvent>&) override {}
		bool sendMidi(const synthLib::SMidiEvent&,
			std::vector<synthLib::SMidiEvent>&) override
		{
			return true;
		}
		void processAudio(const synthLib::TAudioInputs& _inputs,
			const synthLib::TAudioOutputs& _outputs, const size_t _samples) override
		{
			for(uint32_t channel = 0; channel < 2; ++channel)
				for(size_t sample = 0; sample < _samples; ++sample)
					m_receivedOnlySilence = m_receivedOnlySilence
						&& _inputs[channel][sample] == 0.0f;
			for(uint32_t channel = 0; channel < 6; ++channel)
				std::fill_n(_outputs[channel], _samples, 0.75f);
		}

	private:
		bool m_receivedOnlySilence = true;
	};

	struct AudioStorage
	{
		static constexpr size_t Capacity = 4096;
		std::array<float, Capacity> leftInput{};
		std::array<float, Capacity> rightInput{};
		std::array<std::array<float, Capacity>, 6> output{};
		synthLib::TAudioInputs inputs{
			leftInput.data(), rightInput.data(), nullptr, nullptr};
		synthLib::TAudioOutputs outputs{};

		AudioStorage()
		{
			for(size_t channel = 0; channel < output.size(); ++channel)
				outputs[channel] = output[channel].data();
			for(size_t sample = 0; sample < Capacity; ++sample)
			{
				leftInput[sample] = static_cast<float>(sample + 1) / Capacity;
				rightInput[sample] = -leftInput[sample];
			}
		}

		void clearOutputs()
		{
			for(auto& channel : output)
				channel.fill(0.0f);
		}
	};

	void verifyRatesAndVariableBlocks()
	{
		constexpr std::array<float, 3> rates{44100.0f, 48000.0f, 96000.0f};
		constexpr std::array<uint32_t, 8> blockSizes{1, 7, 31, 63, 64, 127, 257, 1024};

		for(const auto rate : rates)
		{
				auto device = std::make_unique<SyntheticAudioDevice>(2, 6, 19);
				synthLib::Plugin plugin(device.get(),
					[](synthLib::Device*) {});
				plugin.setHostSamplerate(rate, 44100.0f);
				plugin.setBlockSize(1024);
				AudioStorage storage;
				std::array<bool, 6> audible{};

				for(const auto blockSize : blockSizes)
				{
					storage.clearOutputs();
					plugin.process(storage.inputs, storage.outputs, blockSize,
						120.0f, 0.0f, true);
					for(size_t channel = 0; channel < storage.output.size(); ++channel)
					{
						for(size_t sample = 0; sample < blockSize; ++sample)
						{
							const auto value = storage.output[channel][sample];
							require(std::isfinite(value),
								"resampler produced a non-finite sample");
							audible[channel] = audible[channel]
								|| std::abs(value) > 0.0001f;
						}
					}
				}
				require(device->receivedEveryInput() && device->receivedEveryOutput(),
					"variable-block processing omitted a device channel");
				require(device->receivedDistinctOutputs(),
					"device output buffers unexpectedly alias each other");
				require(std::all_of(audible.begin(), audible.end(),
					[](const bool _audible) { return _audible; }),
					"variable-block processing dropped an output channel");
		}
	}

	void verifyDeviceReplacementReconfiguresAndFlushes()
	{
		// Plugin::setDevice owns and deletes the device being replaced. The final
		// device remains caller-owned, matching the production replacement contract.
		auto* const initial = new SyntheticAudioDevice(2, 2, 0);
		auto* const replacement = new SyntheticAudioDevice(2, 6, 23);
		synthLib::Plugin plugin(initial,
			[](synthLib::Device*) {});
		plugin.setHostSamplerate(48000.0f, 44100.0f);
		plugin.setBlockSize(64);
		const auto initialLatency = plugin.getLatencyInputToOutput();
		plugin.setDevice(replacement);
		require(plugin.getLatencyInputToOutput() > initialLatency,
			"device replacement did not refresh input latency");

		AudioStorage storage;
		for(uint32_t block = 0; block < 4; ++block)
			plugin.process(storage.inputs, storage.outputs, 256, 120.0f, 0.0f, true);
		require(replacement->receivedEveryOutput(),
			"replacement topology omitted an output channel");
		for(const auto& channel : storage.output)
			require(std::any_of(channel.begin(), channel.begin() + 256,
				[](const float _sample) { return std::abs(_sample) > 0.0001f; }),
				"replacement topology dropped an output channel");

		auto silent = std::make_unique<SyntheticAudioDevice>(2, 6, 0, true);
		plugin.setDevice(silent.get());
		storage.clearOutputs();
		plugin.process(storage.inputs, storage.outputs, 1024, 120.0f, 0.0f, true);
		for(const auto& channel : storage.output)
			require(std::none_of(channel.begin(), channel.begin() + 1024,
				[](const float _sample) { return std::abs(_sample) > 0.0001f; }),
				"replacement emitted stale resampler audio");
	}

	void verifyCappedLatencyIsReportedExactly()
	{
		auto device = std::make_unique<SyntheticAudioDevice>(2, 6, 64);
		synthLib::Plugin plugin(device.get(),
			[](synthLib::Device*) {});
		plugin.setHostSamplerate(44100.0f, 44100.0f);
		plugin.setBlockSize(4096);
		plugin.setLatencyBlocks(8);
		require(device->getExtraLatencySamples() == 16384,
			"device latency test did not reach the realizable cap");
		require(plugin.getLatencyMidiToOutput() == 16384,
			"host MIDI latency did not use the capped device delay");
		require(plugin.getLatencyInputToOutput() == 16384 + 64,
			"host input latency did not use the capped device delay");
		for(uint32_t rate : {48000u, 96000u})
		{
			plugin.setHostSamplerate(float(rate), 44100.0f);
			plugin.setBlockSize(128);
			plugin.setLatencyBlocks(0);
			const auto midiBase = plugin.getLatencyMidiToOutput();
			const auto audioBase = plugin.getLatencyInputToOutput();
			plugin.setLatencyBlocks(1);
			const auto native = (uint64_t{128} * 44100 + rate - 1) / rate;
			const auto host = (native * rate + 44099) / 44100;
			require(device->getExtraLatencySamples() == native
				&& plugin.getLatencyMidiToOutput() == midiBase + host
				&& plugin.getLatencyInputToOutput() == audioBase + host,
				"resampled extra latency rounded early or changed a filter delay");
			plugin.setLatencyBlocks(std::numeric_limits<uint32_t>::max());
			require(device->getExtraLatencySamples() == 16384,
				"large latency-block setting overflowed before the device cap");
		}
	}

	void verifyMissingInputsCannotReadDiscardedOutputs()
	{
		auto device = std::make_unique<MissingInputProbeDevice>();
		synthLib::Plugin plugin(device.get(),
			[](synthLib::Device*) {});
		plugin.setHostSamplerate(44100.0f, 44100.0f);
		plugin.setBlockSize(64);
		const synthLib::TAudioInputs inputs{};
		const synthLib::TAudioOutputs outputs{};
		plugin.process(inputs, outputs, 64, 120.0f, 0.0f, true);
		plugin.process(inputs, outputs, 64, 120.0f, 0.0f, true);
		require(device->receivedOnlySilence(),
			"discarded output leaked into a disabled audio input");
	}

	void verifyInvalidDeviceOnlyNotifiesDuringProcess()
	{
		auto initial = std::make_unique<SyntheticAudioDevice>(2, 2, 0);
		bool notified = false;
		synthLib::Plugin plugin(initial.get(),
			[&](synthLib::Device* const _device)
			{
				require(_device == initial.get(),
					"invalid-device callback received the wrong device");
				notified = true;
			});
		plugin.setHostSamplerate(48000.0f, 44100.0f);
		plugin.setBlockSize(64);
		initial->invalidate();
		AudioStorage storage;
		plugin.process(storage.inputs, storage.outputs, 256, 120.0f, 0.0f, true);
		require(notified && plugin.getDevice() == initial.get(),
			"invalid-device notification mutated topology on the audio thread");
	}

	void verifyRealtimeSysexIsExplicitFallback()
	{
		auto device = std::make_unique<SyntheticAudioDevice>(2, 6, 0);
		synthLib::Plugin plugin(device.get(), [](synthLib::Device*) {});
		plugin.setHostSamplerate(44100.0f, 44100.0f);
		plugin.setBlockSize(64);
		plugin.reserveMidiEventCapacity();
		constexpr std::array<uint8_t, 6> bytes{
			0xf0, 0x7e, 0x7f, 0x06, 0x01, 0xf7};
		synthLib::SMidiEvent sysex(synthLib::MidiEventSource::Host);
		require(sysex.assignRawData(bytes.data(), bytes.size(),
			synthLib::MidiEventSource::Host, 0),
			"could not create SysEx fallback fixture");
		plugin.addMidiEvent(sysex);
		const auto fallbacks = plugin.getRealtimeAllocationFallbackCount();
		AudioStorage storage;
		plugin.process(storage.inputs, storage.outputs, 64, 0.0f, 0.0f, false);
		require(plugin.getRealtimeAllocationFallbackCount() > fallbacks,
			"core SysEx allocation-capable path was not explicitly accounted");
	}

	void verifyTryWithDeviceLockedNeverWaits()
	{
		auto device = std::make_unique<SyntheticAudioDevice>(2, 2, 0);
		synthLib::Plugin plugin(device.get(), [](synthLib::Device*) {});
		std::atomic<bool> lockHeld{false};
		std::atomic<bool> releaseLock{false};

		std::thread holder([&]
		{
			plugin.withDeviceLocked([&](synthLib::Device*)
			{
				lockHeld.store(true, std::memory_order_release);
				while(!releaseLock.load(std::memory_order_acquire))
					std::this_thread::yield();
				return true;
			});
		});
		while(!lockHeld.load(std::memory_order_acquire))
			std::this_thread::yield();

		const auto busyResult = plugin.tryWithDeviceLocked(
			[](synthLib::Device*) { return 7; });
		releaseLock.store(true, std::memory_order_release);
		holder.join();
		require(!busyResult,
			"non-blocking device lookup succeeded while the realtime lock was held");

		const auto availableResult = plugin.tryWithDeviceLocked(
			[](synthLib::Device*) { return 7; });
		require(availableResult && *availableResult == 7,
			"non-blocking device lookup failed after the realtime lock was released");
	}

	void verifyPreparedProcessingDoesNotAllocate()
	{
		constexpr std::array<float, 3> rates{44100.0f, 48000.0f, 96000.0f};
		constexpr std::array<uint32_t, 6> blocks{1, 7, 63, 257, 1024, 2048};

		for(const auto rate : rates)
		{
				auto device = std::make_unique<SyntheticAudioDevice>(2, 6, 0);
				synthLib::Plugin plugin(device.get(),
					[](synthLib::Device*) {});
				plugin.setHostSamplerate(rate, 44100.0f);
				plugin.setBlockSize(2048);
				plugin.reserveMidiEventCapacity();
				AudioStorage storage;
				const synthLib::SMidiEvent noteOn(synthLib::MidiEventSource::Host,
					synthLib::M_NOTEON, 60, 100, 0);

				// Exercise enough maximum-sized blocks to settle filter history before
				// making heap activity a test failure.
				for(size_t warmup = 0; warmup < 8; ++warmup)
					plugin.process(storage.inputs, storage.outputs, 2048,
						0.0f, 0.0f, false);

				for(const auto block : blocks)
				{
					auto inputs = storage.inputs;
					auto outputs = storage.outputs;
					// Cover the disabled-input and hidden-output scratch paths too.
					inputs[1] = nullptr;
					outputs[2] = nullptr;
					outputs[3] = nullptr;
					g_detectedAllocationCount = 0;
					g_detectAllocations = true;
					plugin.addMidiEvent(noteOn);
					{
						synthLib::RealtimeInstrumentation::CallbackScope capture(
							plugin.getRealtimeInstrumentation(), block, rate);
						capture.setHostState(false, false);
						synthLib::RealtimeInstrumentation::recordCurrentPanelDelivery(1, 0x25, 1);
						plugin.process(inputs, outputs, block, 0.0f, 0.0f, false);
					}
					g_detectAllocations = false;
					if(g_detectedAllocationCount != 0)
						std::cerr << "allocation probe: rate=" << rate
							<< " block=" << block << " allocations="
							<< g_detectedAllocationCount << '\n';
					require(g_detectedAllocationCount == 0,
						"prepared audio processing allocated from the heap");
				}

				const auto fallbacks = plugin.getRealtimeAllocationFallbackCount();
				plugin.process(storage.inputs, storage.outputs, 2049,
					0.0f, 0.0f, false);
				require(plugin.getRealtimeAllocationFallbackCount() == fallbacks + 1,
					"oversized host block was not identified as an RT fallback");
				require(device->receivedDistinctOutputs(),
					"hidden outputs shared one discard buffer");
		}
	}
}

int main()
{
	try
	{
		verifyRatesAndVariableBlocks();
		verifyDeviceReplacementReconfiguresAndFlushes();
		verifyCappedLatencyIsReportedExactly();
		verifyMissingInputsCannotReadDiscardedOutputs();
		verifyInvalidDeviceOnlyNotifiesDuringProcess();
		verifyRealtimeSysexIsExplicitFallback();
		verifyTryWithDeviceLockedNeverWaits();
		verifyPreparedProcessingDoesNotAllocate();
		std::cout << "synthLibAudioTest: PASS\n";
		return 0;
	}
	catch(const std::exception& _error)
	{
		std::cerr << "synthLibAudioTest: " << _error.what() << '\n';
		return 1;
	}
}
