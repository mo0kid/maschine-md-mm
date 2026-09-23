#include "mdLib/mddevice.h"
#include "mdLib/mdromloader.h"
#include "mdLib/mdtypes.h"

#include "baseLib/filesystem.h"
#include "synthLib/plugin.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
	void require(const bool _condition, const char* const _message)
	{
		if(!_condition)
			throw std::runtime_error(_message);
	}

	uint32_t nextRandom(uint32_t& _state)
	{
		_state ^= _state << 13;
		_state ^= _state >> 17;
		_state ^= _state << 5;
		return _state;
	}

	void queueExerciseMidi(synthLib::Plugin& _plugin, const uint32_t _block,
		const uint32_t _iteration, const md::MachineModel _model)
	{
		if((_iteration % 23) == 0)
		{
			const auto note = _model == md::MachineModel::Monomachine ? 60 : 36;
			_plugin.addMidiEvent({synthLib::MidiEventSource::Host,
				static_cast<uint8_t>((_iteration & 1) ? synthLib::M_NOTEOFF
					: synthLib::M_NOTEON), static_cast<uint8_t>(note), 96,
				_block / 2});
		}
		if((_iteration % 41) == 0)
			_plugin.addMidiEvent({synthLib::MidiEventSource::Host,
				synthLib::M_CONTROLCHANGE, synthLib::MC_MODULATION,
				static_cast<uint8_t>(_iteration & 0x7f), _block / 3});
		if((_iteration % 97) == 0)
		{
			synthLib::SMidiEvent identity(synthLib::MidiEventSource::Host);
			constexpr std::array<uint8_t, 6> bytes{0xf0, 0x7e, 0x7f, 0x06, 0x01, 0xf7};
			identity.assignRawData(bytes.data(), bytes.size(),
				synthLib::MidiEventSource::Host, _block / 4);
			_plugin.addMidiEvent(identity);
		}
	}

	void verifyFirmware(const char* const _label, const char* const _path,
		const md::MachineModel _model)
	{
		std::vector<uint8_t> firmware;
		require(baseLib::filesystem::readFile(firmware, _path),
			"could not read pinned firmware image");
		// This fingerprints the exact canonical MD 1.63/MM 1.32b image.
		require(md::RomLoader::isRomForModel(firmware, _model),
			"firmware was not the pinned canonical image for this model");

		synthLib::DeviceCreateParams params;
		params.romData = std::move(firmware);
		params.romName = _path;
		params.customData = md::deviceCustomData(_model);
		auto device = std::make_unique<md::Device>(params);
		require(device->isValid(), "pinned firmware did not create a valid device");
		synthLib::Plugin plugin(device.get(),
			[](synthLib::Device*) {});
		plugin.reserveMidiEventCapacity();

		constexpr size_t capacity = 2048;
		std::array<float, capacity> left{};
		std::array<float, capacity> right{};
		std::array<std::array<float, capacity>, 6> output{};
		const synthLib::TAudioInputs inputs{
			left.data(), right.data(), nullptr, nullptr};
		synthLib::TAudioOutputs outputs{};
		for(size_t channel = 0; channel < output.size(); ++channel)
			outputs[channel] = output[channel].data();

		constexpr std::array<float, 3> rates{44100.0f, 48000.0f, 96000.0f};
		constexpr std::array<uint32_t, 11> hostileBlocks{
			1, 2, 7, 31, 63, 64, 65, 127, 257, 1023, 2048};
		uint32_t random = _model == md::MachineModel::Monomachine
			? 0x132b5eedu : 0x16305eedu;
		uint64_t inputCursor = 0;
		bool exercisedState = false;

		for(const auto rate : rates)
		{
				plugin.setHostSamplerate(rate, 44100.0f);
				plugin.setBlockSize(capacity);
				for(size_t warmup = 0; warmup < 4; ++warmup)
					plugin.process(inputs, outputs, 1024, 0.0f, 0.0f, false);

#if SYNTHLIB_DEMO_MODE == 0
				if(!exercisedState)
				{
					std::vector<uint8_t> state;
					require(plugin.getState(state, synthLib::StateTypeGlobal)
						&& !state.empty(), "canonical firmware state capture failed");
					require(plugin.setState(state),
						"canonical firmware state round-trip failed");
					exercisedState = true;
				}
#endif

				device->getHardware().resetHostAudioInputQueueTelemetry();
				uint32_t remaining = 32768;
				uint32_t iteration = 0;
				std::vector<synthLib::SMidiEvent> midiOut;
				while(remaining)
				{
					const auto selected = hostileBlocks[nextRandom(random)
						% hostileBlocks.size()];
					const auto block = std::min(selected, remaining);
					for(size_t sample = 0; sample < block; ++sample)
					{
						const auto phase = static_cast<float>(inputCursor + sample);
						left[sample] = 0.2f * std::sin(phase * 0.017f);
						right[sample] = 0.15f * std::cos(phase * 0.013f);
					}
					queueExerciseMidi(plugin, block, iteration, _model);
					plugin.process(inputs, outputs, block, 123.0f,
						static_cast<float>(inputCursor) / rate, true);
					plugin.getMidiOut(midiOut);
					for(const auto& channel : output)
						for(size_t sample = 0; sample < block; ++sample)
							require(std::isfinite(channel[sample]),
								"firmware/resampler produced a non-finite sample");
					inputCursor += block;
					remaining -= block;
					++iteration;
				}

				require(device->getHardware().hostAudioInputUnderflowCount() == 0,
					"firmware scheduler outran host-input lookahead");
				require(device->getHardware().hostAudioInputOverflowCount() == 0,
					"firmware scheduler overflowed the host-input queue");
		}
		std::cout << "mdAudioFirmwareTest: " << _label
			<< " randomized scheduler/resampler soak PASS\n";
	}
}

int main()
{
	const auto* const mdPath = std::getenv("GEARMULATOR_MD_FIRMWARE_BIN");
	const auto* const mmPath = std::getenv("GEARMULATOR_MM_FIRMWARE_BIN");
	if((!mdPath || !*mdPath) && (!mmPath || !*mmPath))
	{
		std::cout << "mdAudioFirmwareTest: SKIP (pinned firmware not supplied)\n";
		return 77;
	}

	try
	{
		if(mdPath && *mdPath)
			verifyFirmware("MD 1.63", mdPath, md::MachineModel::Machinedrum);
		if(mmPath && *mmPath)
			verifyFirmware("MM 1.32b", mmPath, md::MachineModel::Monomachine);
		return 0;
	}
	catch(const std::exception& _error)
	{
		std::cerr << "mdAudioFirmwareTest: " << _error.what() << '\n';
		return 1;
	}
}
