#include "mdCombinedProcessor.h"

#include "mdCombinedEditor.h"
#include "mdLib/mddevice.h"

#include "dsp56kBase/threadtools.h"

#include <algorithm>
#include <chrono>
#include <cstdio>

namespace mdJucePlugin
{
	namespace
	{
		constexpr uint32_t g_stateMagic = 0x4d444d4d;
		constexpr int g_stateVersion = 1;

		void prepareChild(juce::AudioProcessor& _processor,
			const double _sampleRate, const int _blockSize)
		{
			_processor.setPlayConfigDetails(2, 2, _sampleRate, _blockSize);
			_processor.prepareToPlay(_sampleRate, _blockSize);
		}
	}

	CombinedProcessor::CombinedProcessor()
		: AudioProcessor(BusesProperties()
			.withInput("Input A/B", juce::AudioChannelSet::stereo(), true)
			.withOutput("Main A/B", juce::AudioChannelSet::stereo(), true))
		, m_machinedrum(md::MachineModel::Machinedrum, false)
		, m_monomachine(md::MachineModel::Monomachine, false)
		, m_maschine(m_machinedrum, m_monomachine)
	{
		m_machinedrum.setForceSoftwareRendererForSession(true);
		m_monomachine.setForceSoftwareRendererForSession(true);
		m_mmWorker = std::thread([this] { runMonomachineWorker(); });
	}

	CombinedProcessor::~CombinedProcessor()
	{
		stopFastBootWorkers();
		m_stoppingWorker.store(true, std::memory_order_release);
		m_mmWorkReady.signal();
		if(m_mmWorker.joinable())
			m_mmWorker.join();
	}

	void CombinedProcessor::stopFastBootWorkers()
	{
		m_stopFastBoot.store(true, std::memory_order_release);
		if(m_mdFastBootWorker.joinable())
			m_mdFastBootWorker.join();
		if(m_mmFastBootWorker.joinable())
			m_mmFastBootWorker.join();
		m_mdFastBootActive.store(false, std::memory_order_release);
		m_mmFastBootActive.store(false, std::memory_order_release);
	}

	void CombinedProcessor::runFastBoot(AudioPluginAudioProcessor& _processor,
		std::atomic<bool>& _active)
	{
		constexpr uint32_t chunkFrames = 256;
		constexpr uint32_t maximumFrames = md::g_samplerate * 25;
		constexpr uint32_t stableFrames = md::g_samplerate * 3 / 2;
		auto& plugin = _processor.getPlugin();
		const auto started = std::chrono::steady_clock::now();
		uint32_t frames = 0;
		uint32_t readySince = 0;
		bool finished = false;
		bool active = false;

		while(!m_stopFastBoot.load(std::memory_order_acquire)
			&& std::chrono::steady_clock::now() - started < std::chrono::seconds(30)
			&& frames < maximumFrames && !finished)
		{
			// A restored project may still be booting its replacement hardware.
			// Let its normal audio path finish before taking ownership of boot.
			const auto state = plugin.tryWithDeviceLocked(
				[](synthLib::Device* const _device)
				{
					const auto* const device = dynamic_cast<md::Device*>(_device);
					if(!device || !device->isValid()) return -1;
					if(device->isProjectStateRestorePending()) return 0;
					if(device->getHardware().isFactoryFlashInitializationExpected())
						return -1;
					if(device->getHardware().isFirmwareMidiReady()
						&& device->getFrontPanelSnapshot().countLitPixels() >= 2000)
						return 2;
					return 1;
				});
			if(!state || *state == 0)
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(2));
				continue;
			}
			if(*state < 0)
				break;
			if(*state == 2 && !active)
				break;

			if(!active)
			{
				_active.store(true, std::memory_order_release);
				active = true;
			}

			const auto advanced = plugin.withDeviceLocked(
				[&](synthLib::Device* const _device)
				{
					auto* const device = dynamic_cast<md::Device*>(_device);
					if(!device || device->isProjectStateRestorePending())
						return false;
					device->getHardware().advance(chunkFrames);
					return true;
				});
			if(!advanced)
				break;
			frames += chunkFrames;

			if((frames % 2048) != 0)
				continue;
			finished = plugin.withDeviceLocked(
				[&](synthLib::Device* const _device)
				{
					const auto* const device = dynamic_cast<md::Device*>(_device);
					if(!device) return false;
					const auto& hardware = device->getHardware();
					const bool ready = hardware.isFirmwareMidiReady()
						&& hardware.getFrontPanelSnapshot().countLitPixels() >= 2000;
					if(!ready)
						readySince = 0;
					else if(readySince == 0)
						readySince = frames;
					return readySince != 0 && frames - readySince >= stableFrames;
				});
		}

		_active.store(false, std::memory_order_release);
		if(active)
		{
			const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::steady_clock::now() - started).count();
			std::fprintf(stderr, "[MD-MM] %s fast boot: %u frames in %lld ms (%s)\n",
				_processor.getModel() == md::MachineModel::Machinedrum ? "MD" : "MM",
				frames, static_cast<long long>(elapsed), finished ? "ready" : "fallback");
		}
	}

	void CombinedProcessor::runMonomachineWorker()
	{
		dsp56k::ThreadTools::setCurrentThreadPriority(
			dsp56k::ThreadPriority::Highest);
		for(;;)
		{
			m_mmWorkReady.wait();
			if(m_stoppingWorker.load(std::memory_order_acquire))
				return;
			static_cast<juce::AudioProcessor&>(m_monomachine).processBlock(
				m_mmAudio, m_mmMidi);
			m_mmWorkFinished.signal();
		}
	}

	bool CombinedProcessor::isBusesLayoutSupported(
		const BusesLayout& _layouts) const
	{
		return _layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo()
			&& (_layouts.getMainInputChannelSet().isDisabled()
				|| _layouts.getMainInputChannelSet() == juce::AudioChannelSet::stereo());
	}

	void CombinedProcessor::prepareToPlay(const double _sampleRate,
		const int _maximumBlockSize)
	{
		stopFastBootWorkers();
		m_maximumBlockSize = std::max(1, _maximumBlockSize);
		m_mdAudio.setSize(2, m_maximumBlockSize, false, true);
		m_mmAudio.setSize(2, m_maximumBlockSize, false, true);
		m_mdMidi.ensureSize(4096);
		m_mmMidi.ensureSize(4096);
		m_midiRouter.reset();
		prepareChild(m_machinedrum, _sampleRate, m_maximumBlockSize);
		prepareChild(m_monomachine, _sampleRate, m_maximumBlockSize);
		setLatencySamples(std::max(m_machinedrum.getLatencySamples(),
			m_monomachine.getLatencySamples()));
		if(juce::JUCEApplicationBase::isStandaloneApp())
		{
			m_stopFastBoot.store(false, std::memory_order_release);
			m_mdFastBootWorker = std::thread([this]
				{ runFastBoot(m_machinedrum, m_mdFastBootActive); });
			m_mmFastBootWorker = std::thread([this]
				{ runFastBoot(m_monomachine, m_mmFastBootActive); });
		}
	}

	void CombinedProcessor::releaseResources()
	{
		stopFastBootWorkers();
		static_cast<juce::AudioProcessor&>(m_machinedrum).releaseResources();
		static_cast<juce::AudioProcessor&>(m_monomachine).releaseResources();
	}

	void CombinedProcessor::processBlock(juce::AudioBuffer<float>& _audio,
		juce::MidiBuffer& _midi)
	{
		juce::ScopedNoDenormals noDenormals;
		const auto samples = _audio.getNumSamples();
		if(samples <= 0)
			return;
		if(samples > m_maximumBlockSize || _audio.getNumChannels() < 2)
		{
			_audio.clear();
			_midi.clear();
			return;
		}

		m_mdAudio.setSize(2, samples, false, false, true);
		m_mmAudio.setSize(2, samples, false, false, true);
		for(int channel = 0; channel < 2; ++channel)
		{
			m_mdAudio.copyFrom(channel, 0, _audio, channel, 0, samples);
			m_mmAudio.copyFrom(channel, 0, _audio, channel, 0, samples);
		}
		m_mdMidi.clear();
		m_mmMidi.clear();
		const auto focused = m_maschine.focusedModel();
		const auto selectedMmTrack = m_maschine.selectedMonomachineTrack();
		const auto mmBaseChannel = m_monomachine.getMidiBaseChannel();
		for(const auto event : _midi)
		{
			const auto message = event.getMessage();
			const auto* const bytes = message.getRawData();
			const auto size = message.getRawDataSize();
			if(!bytes || size <= 0 || event.samplePosition >= samples)
				continue;
			const auto destinations = m_midiRouter.route(bytes[0],
				size > 1 ? bytes[1] : 0, size > 2 ? bytes[2] : 0, focused);
			if(destinations & CombinedMidiRouter::machinedrum)
				m_mdMidi.addEvent(message, event.samplePosition);
			if(destinations & CombinedMidiRouter::monomachine)
			{
				const auto channels = m_midiRouter.monomachineChannels(bytes[0],
					size > 1 ? bytes[1] : 0, size > 2 ? bytes[2] : 0,
					selectedMmTrack, mmBaseChannel);
				if(channels == 0)
					m_mmMidi.addEvent(message, event.samplePosition);
				else
				{
					for(int channel = 0; channel < 16; ++channel)
					{
						if((channels & (1u << channel)) == 0)
							continue;
						auto routed = message;
						routed.setChannel(channel + 1);
						m_mmMidi.addEvent(routed, event.samplePosition);
					}
				}
			}
		}

		// The machines do not share mutable emulation state. Run MM on its persistent
		// high-priority worker while the host audio thread runs MD, then join at the
		// block boundary before mixing. This keeps the heavier engine from serially
		// consuming the other engine's deadline budget.
		const bool processMm = !m_mmFastBootActive.load(std::memory_order_acquire);
		if(processMm)
			m_mmWorkReady.signal();
		if(!m_mdFastBootActive.load(std::memory_order_acquire))
			static_cast<juce::AudioProcessor&>(m_machinedrum).processBlock(
				m_mdAudio, m_mdMidi);
		else
		{
			m_mdAudio.clear();
			m_mdMidi.clear();
		}
		if(processMm)
			m_mmWorkFinished.wait();
		else
		{
			m_mmAudio.clear();
			m_mmMidi.clear();
		}

		for(int channel = 0; channel < 2; ++channel)
		{
			_audio.copyFrom(channel, 0, m_mdAudio, channel, 0, samples);
			_audio.applyGain(channel, 0, samples, 0.5f);
			_audio.addFrom(channel, 0, m_mmAudio, channel, 0, samples, 0.5f);
		}
		_midi.clear();
		_midi.addEvents(m_mdMidi, 0, samples, 0);
		_midi.addEvents(m_mmMidi, 0, samples, 0);
	}

	juce::AudioProcessorEditor* CombinedProcessor::createEditor()
	{
		return new CombinedEditor(*this);
	}

	void CombinedProcessor::getStateInformation(juce::MemoryBlock& _destination)
	{
		juce::MemoryBlock mdState;
		juce::MemoryBlock mmState;
		static_cast<juce::AudioProcessor&>(m_machinedrum).getStateInformation(mdState);
		static_cast<juce::AudioProcessor&>(m_monomachine).getStateInformation(mmState);
		juce::MemoryOutputStream stream(_destination, false);
		stream.writeInt(static_cast<int>(g_stateMagic));
		stream.writeInt(g_stateVersion);
		stream.writeByte(m_maschine.focusedModel() == md::MachineModel::Monomachine
			? 1 : 0);
		stream.writeInt(static_cast<int>(mdState.getSize()));
		stream.write(mdState.getData(), mdState.getSize());
		stream.writeInt(static_cast<int>(mmState.getSize()));
		stream.write(mmState.getData(), mmState.getSize());
	}

	void CombinedProcessor::setStateInformation(const void* const _data,
		const int _size)
	{
		if(!_data || _size <= 0)
			return;
		juce::MemoryInputStream stream(_data, static_cast<size_t>(_size), false);
		if(static_cast<uint32_t>(stream.readInt()) != g_stateMagic
			|| stream.readInt() != g_stateVersion)
			return;
		m_maschine.setFocusedModel(stream.readByte() != 0
			? md::MachineModel::Monomachine : md::MachineModel::Machinedrum);
		const auto mdSize = stream.readInt();
		if(mdSize < 0 || static_cast<int64_t>(mdSize) > stream.getNumBytesRemaining())
			return;
		juce::MemoryBlock mdState(static_cast<size_t>(mdSize));
		if(stream.read(mdState.getData(), mdState.getSize()) != mdSize)
			return;
		const auto mmSize = stream.readInt();
		if(mmSize < 0 || static_cast<int64_t>(mmSize) > stream.getNumBytesRemaining())
			return;
		juce::MemoryBlock mmState(static_cast<size_t>(mmSize));
		if(stream.read(mmState.getData(), mmState.getSize()) != mmSize)
			return;
		static_cast<juce::AudioProcessor&>(m_machinedrum).setStateInformation(
			mdState.getData(), static_cast<int>(mdState.getSize()));
		static_cast<juce::AudioProcessor&>(m_monomachine).setStateInformation(
			mmState.getData(), static_cast<int>(mmState.getSize()));
	}
}
