#include "processor.h"

#include <algorithm>
#include <chrono>

#include "dummydevice.h"
#include "midiLearnManager.h"
#include "pluginVersion.h"
#include "tools.h"
#include "types.h"

#include "baseLib/binarystream.h"
#include "baseLib/filesystem.h"

#include "bridgeLib/commands.h"

#include "client/remoteDevice.h"

#include "synthLib/deviceException.h"
#include "synthLib/os.h"
#include "synthLib/midiBufferParser.h"
#include "synthLib/romLoader.h"

#include "dsp56kBase/fastmath.h"
#include "dsp56kBase/logging.h"

#include "juceUiLib/messageBox.h"

namespace synthLib
{
	class DeviceException;
}

namespace pluginLib
{
	constexpr char g_saveMagic[] = "DSP56300";
	constexpr uint32_t g_saveVersion = 2;
	constexpr const char* const g_defaultProgramName = "default";

	bridgeLib::SessionId generateRemoteSessionId()
	{
		return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
	}

	Processor::Processor(const BusesProperties& _busesProperties, Properties _properties)
		: juce::AudioProcessor(_busesProperties)
		, m_properties(std::move(_properties))
		, m_midiPorts(*this)
		, m_remoteSessionId(generateRemoteSessionId())
		, m_programName(g_defaultProgramName)
	{
		juce::File(getPublicRomFolder()).createDirectory();

		synthLib::RomLoader::addSearchPath(getPublicRomFolder());
		synthLib::RomLoader::addSearchPath(synthLib::getModulePath(true));
		synthLib::RomLoader::addSearchPath(synthLib::getModulePath(false));
	}

	Processor::~Processor()
	{
		cancelPendingUpdate();
		m_midiPorts.close();
		destroyController();
		m_plugin.reset();
		m_device.reset();
	}

	void Processor::addMidiEvent(const synthLib::SMidiEvent& _ev)
	{
		// Process through MIDI Learn translator first
		if (_ev.source != synthLib::MidiEventSource::Device)
		{
			if (m_midiLearnTranslator && m_midiLearnTranslator->processMidiInput(_ev))
			{
				// MIDI event was consumed by MIDI Learn (learned mapping or learning mode)
				return;
			}

			if (m_midiRoutingMatrix.enabled(_ev, synthLib::MidiEventSource::Device))
			{
				if (m_programChangeRouter.processMidiEvent(_ev))
				{
					// Program change was handled by patch manager
					return;
				}
			}
		}

		if (m_controller)
			m_controller->tryEnqueueRealtimeMidiMessage(_ev);
		if (m_midiRoutingMatrix.enabled(_ev, synthLib::MidiEventSource::Device))
			getPlugin().addMidiEvent(_ev);
		if (m_midiRoutingMatrix.enabled(_ev, synthLib::MidiEventSource::Physical))
			m_midiPorts.send(_ev);
	}

	bool Processor::tryAddRealtimeMidiEvent(const synthLib::SMidiEvent& _ev)
	{
		// Physical output is attempted first: once queued, insertion into the local
		// synth is allocation-free because prepareToPlay reserves both the normal
		// ingress capacity and the controller's bounded realtime batch.
		// Plugin::processMidiInEvents runs before the controller drain in processBlock.
		// Consequently, a synchronization request queued by the controller/message
		// thread is already in the device FIFO before a later host publication is
		// inserted here. Equal-offset insertions append, preserving that wire order;
		// dump-request revision watermarks rely on this ordering.
		if(m_midiRoutingMatrix.enabled(_ev, synthLib::MidiEventSource::Physical)
			&& !m_midiPorts.trySend(_ev))
			return false;
		if(m_midiRoutingMatrix.enabled(_ev, synthLib::MidiEventSource::Device))
			getPlugin().insertMidiEvent(_ev);
		return true;
	}

	void Processor::handleIncomingMidiMessage(juce::MidiInput *_source, const juce::MidiMessage &_message)
	{
		synthLib::SMidiEvent sm(synthLib::MidiEventSource::Physical);

		const auto* raw = _message.getSysExData();
		if (raw)
		{
			const auto count = _message.getSysExDataSize();
			auto syx = SysEx();
			syx.push_back(0xf0);
			for (int i = 0; i < count; i++)
			{
				syx.push_back(raw[i]);
			}
			syx.push_back(0xf7);
			sm.sysex = std::move(syx);
		}
		else
		{
			const auto count = _message.getRawDataSize();
			const auto* rawData = _message.getRawData();
			if (count >= 1 && count <= 3)
			{
				sm.a = rawData[0];
				sm.b = count > 1 ? rawData[1] : 0;
				sm.c = count > 2 ? rawData[2] : 0;
			}
			else
			{
				auto syx = SysEx();
				for (int i = 0; i < count; i++)
					syx.push_back(rawData[i]);
				sm.sysex = syx;
			}
		}

		addMidiEvent(sm);
	}

	Controller& Processor::getController()
	{
	    if (m_controller == nullptr)
		{
	        m_controller.reset(createController());
			
			// Initialize MIDI Learn translator with controller
			if (m_controller && !m_midiLearnTranslator)
			{
				m_midiLearnTranslator = std::make_unique<MidiLearnTranslator>(*m_controller, m_controller->getParameterDescriptions().getControllerMap());
				
				// Setup MIDI feedback callback
				m_midiLearnTranslator->onSendMidiOutput = [this](const synthLib::MidiEventSource _target, const synthLib::SMidiEvent& _event)
				{
					if (_target == synthLib::MidiEventSource::Editor && _event.source != synthLib::MidiEventSource::Editor)
						m_controller->tryEnqueueRealtimeMidiMessage(_event);
					else if (_target == synthLib::MidiEventSource::Physical && _event.source != synthLib::MidiEventSource::Physical)
						m_midiPorts.send(_event);
					else if (_target == synthLib::MidiEventSource::Host && _event.source != synthLib::MidiEventSource::Host)
						addHostMidiFeedback(_event);
				};

				// Load default MIDI learn preset from disk. DAW state restore
				// (setStateInformation) will override this if present.
				loadDefaultMidiLearnPreset();
			}
		}

	    return *m_controller;
	}

	synthLib::Plugin& Processor::getPlugin()
	{
		if(m_plugin)
			return *m_plugin;

		try
		{
			m_device.reset(createDevice());
			if(!m_device->isValid())
				throw synthLib::DeviceException(synthLib::DeviceError::Unknown, "Device initialization failed");
		}
		catch(const synthLib::DeviceException& e)
		{
			LOG("Failed to create device: " << e.what());

			// Juce loads the LV2/VST3 versions of the plugin as part of the build process, if we open a message box in this case, the build process gets stuck
			const auto host = juce::PluginHostType::getHostPath();
			if(!Tools::isHeadless())
			{
				std::string msg = e.what();

				m_deviceError = e.errorCode();

				if(e.errorCode() == synthLib::DeviceError::FirmwareMissing)
				{
					msg += "\n\n";
					msg += "The firmware file needs to be copied to\n";
					msg += baseLib::filesystem::validatePath(getPublicRomFolder()) + "\n";
					msg += "\n";
					msg += "The target folder will be opened once you click OK. Copy the firmware to this folder and reload the plugin.";
#ifdef _DEBUG
					msg += "\n\n" + std::string("[Debug] Host ") + host.toStdString() + "\n\n";
#endif
				}
				juce::Timer::callAfterDelay(2000, [this, msg]
				{
					genericUI::MessageBox::showOk(genericUI::MessageBox::Icon::Warning,
						"Device Initialization failed", msg, 
						[this]
						{
							const auto path = juce::File(getPublicRomFolder());
							(void)path.createDirectory();
							path.revealToUser();
						}
					);
				});
			}
		}

		if(!m_device)
		{
			m_device.reset(new DummyDevice({}));
		}

		m_device->setDspClockPercent(m_dspClockPercent);

		m_plugin.reset(new synthLib::Plugin(m_device.get(), [this](synthLib::Device* _device)
		{
			onDeviceInvalid(_device);
		}));

		return *m_plugin;
	}

	bridgeClient::RemoteDevice* Processor::createRemoteDevice(const synthLib::DeviceCreateParams& _params)
	{
		bridgeLib::PluginDesc desc;
		getPluginDesc(desc);
		return new bridgeClient::RemoteDevice(_params, std::move(desc), m_remoteHost, m_remotePort);
	}

	void Processor::getRemoteDeviceParams(synthLib::DeviceCreateParams& _params) const
	{
		_params.preferredSamplerate = getPreferredDeviceSamplerate();
		_params.hostSamplerate = getHostSamplerate();
	}

	bridgeClient::RemoteDevice* Processor::createRemoteDevice()
	{
		synthLib::DeviceCreateParams params;
		getRemoteDeviceParams(params);
		return createRemoteDevice(params);
	}

	synthLib::Device* Processor::createDevice(const DeviceType _type)
	{
		switch (_type)
		{
		case DeviceType::Local:		return createDevice();
		case DeviceType::Remote:	return createRemoteDevice();
		case DeviceType::Dummy:		return new DummyDevice({});
		}
		return nullptr;
	}

	bool Processor::setLatencyBlocks(uint32_t _blocks)
	{
		if (!getPlugin().setLatencyBlocks(_blocks))
			return false;
		updateLatencySamples();
		return true;
	}

	void Processor::updateLatencySamples()
	{
		if(getProperties().isSynth)
			setLatencySamples(getTotalNumInputChannels() > 0
				? std::max(getPlugin().getLatencyMidiToOutput(),
					getPlugin().getLatencyInputToOutput())
				: getPlugin().getLatencyMidiToOutput());
		else
			setLatencySamples(getPlugin().getLatencyInputToOutput());
	}

	void Processor::handleAsyncUpdate()
	{
		if(m_deviceRecoveryPending.exchange(false))
			recoverInvalidDevice();
		updateLatencySamples();
	}

	void Processor::requestLatencyUpdate()
	{
		auto* const messageManager = juce::MessageManager::getInstanceWithoutCreating();
		if(messageManager && messageManager->isThisTheMessageThread())
			updateLatencySamples();
		else
			triggerAsyncUpdate();
	}

	void Processor::saveCustomData(std::vector<uint8_t>& _targetBuffer)
	{
		baseLib::BinaryStream s;
		saveChunkData(s);
		s.toVector(_targetBuffer, true);
	}

	bool Processor::loadCustomData(const std::vector<uint8_t>& _sourceBuffer)
	{
		if(_sourceBuffer.empty())
			return true;

		// In Vavra, the only data we had was the gain parameters
		if(_sourceBuffer.size() == sizeof(float) * 2 + sizeof(uint32_t))
		{
			baseLib::BinaryStream ss(_sourceBuffer);
			readGain(ss);
			return true;
		}

		baseLib::BinaryStream s(_sourceBuffer);
		baseLib::ChunkReader cr(s);

		loadChunkData(cr);

		return _sourceBuffer.empty() || (cr.tryRead() && cr.numRead() > 0);
	}

	void Processor::saveChunkData(baseLib::BinaryStream& s)
	{
		// it is important that this is stored before other chunks to restore state to the remote properly
		if (m_deviceType == DeviceType::Remote)
		{
			baseLib::ChunkWriter cw(s, "REMO", 1);
			s.write(static_cast<int32_t>(m_deviceType));
			s.write(m_remoteHost);
			s.write(m_remotePort);
		}

		{
			std::vector<uint8_t> buffer;
			getPlugin().getState(buffer, synthLib::StateTypeGlobal);

			baseLib::ChunkWriter cw(s, "MIDI", 1);
			s.write(buffer);
		}
		{
			baseLib::ChunkWriter cw(s, "GAIN", 1);
			s.write<uint32_t>(1);	// version
			s.write(m_inputGain);
			s.write(getOutputGain());
		}

		if(m_dspClockPercent != 100)
		{
			baseLib::ChunkWriter cw(s, "DSPC", 1);
			s.write(m_dspClockPercent);
		}

		if(m_preferredDeviceSamplerate > 0)
		{
			baseLib::ChunkWriter cw(s, "DSSR", 1);
			s.write(m_preferredDeviceSamplerate);
		}

		m_midiPorts.saveChunkData(s);
		m_midiRoutingMatrix.saveChunkData(s);

		if (m_midiLearnTranslator)
			m_midiLearnTranslator->saveChunkData(s);

		if (m_programName != g_defaultProgramName)
		{
			baseLib::ChunkWriter cw(s, "PROG", 1);
			s.write(m_programName);
		}
	}

	void Processor::loadChunkData(baseLib::ChunkReader& _cr)
	{
		_cr.add("MIDI", 1, [this](baseLib::BinaryStream& _binaryStream, uint32_t _version)
		{
			std::vector<uint8_t> buffer;
			_binaryStream.read(buffer);
			getPlugin().setState(buffer);
		});

		_cr.add("GAIN", 1, [this](baseLib::BinaryStream& _binaryStream, uint32_t _version)
		{
			readGain(_binaryStream);
		});

		_cr.add("DSPC", 1, [this](baseLib::BinaryStream& _binaryStream, uint32_t _version)
		{
			auto p = _binaryStream.read<uint32_t>();
			p = dsp56k::clamp<uint32_t>(p, 50, 200);
			setDspClockPercent(p);
		});

		_cr.add("DSSR", 1, [this](baseLib::BinaryStream& _binaryStream, uint32_t _version)
		{
			const auto sr = _binaryStream.read<float>();
			setPreferredDeviceSamplerate(sr);
		});

		_cr.add("RSMP", 1, [](baseLib::BinaryStream& _binaryStream, uint32_t _version)
		{
			// Retired resampler selection retained as a read-only compatibility chunk.
			(void)_binaryStream.read<uint8_t>();
		});

		_cr.add("PROG", 1, [this](baseLib::BinaryStream& _binaryStream, uint32_t _version)
		{
			m_programName = _binaryStream.readString();
		});

		_cr.add("REMO", 1, [this](baseLib::BinaryStream& _binaryStream, uint32_t _version)
		{
			const auto type = static_cast<DeviceType>(_binaryStream.read<int32_t>());
			const auto host = _binaryStream.readString();
			const auto port = _binaryStream.read<uint32_t>();
			if (type == DeviceType::Remote)
				setRemoteDevice(host, port);
		});

		m_midiPorts.loadChunkData(_cr);
		m_midiRoutingMatrix.loadChunkData(_cr);
		
		if (m_midiLearnTranslator)
			m_midiLearnTranslator->loadChunkData(_cr);
	}

	void Processor::readGain(baseLib::BinaryStream& _s)
	{
		const auto version = _s.read<uint32_t>();
		if (version != 1)
			return;
		m_inputGain = _s.read<float>();
		setOutputGain(_s.read<float>());
	}

	bool Processor::setDspClockPercent(const uint32_t _percent)
	{
		if(!m_device)
			return false;
		if(!m_device->setDspClockPercent(_percent))
			return false;
		m_dspClockPercent = _percent;
		return true;
	}

	uint32_t Processor::getDspClockPercent() const
	{
		if(!m_device)
			return m_dspClockPercent;
		return m_device->getDspClockPercent();
	}

	uint64_t Processor::getDspClockHz() const
	{
		if(!m_device)
			return 0;
		return m_device->getDspClockHz();
	}

	bool Processor::canModifyDspClock() const
	{
		if(!m_device)
			return false;
		return m_device->canModifyDspClock();
	}

	bool Processor::setPreferredDeviceSamplerate(const float _samplerate)
	{
		m_preferredDeviceSamplerate = _samplerate;

		if(!m_device)
			return false;

		return getPlugin().setPreferredDeviceSamplerate(_samplerate);
	}

	float Processor::getPreferredDeviceSamplerate() const
	{
		return m_preferredDeviceSamplerate;
	}

	std::vector<float> Processor::getDeviceSupportedSamplerates() const
	{
		if(!m_device)
			return {};
		std::vector<float> result;
		m_device->getSupportedSamplerates(result);
		return result;
	}

	std::vector<float> Processor::getDevicePreferredSamplerates() const
	{
		if(!m_device)
			return {};
		std::vector<float> result;
		m_device->getPreferredSamplerates(result);
		return result;
	}

	std::optional<std::pair<const char*, uint32_t>> Processor::findResource(const BinaryDataRef& _binaryData,	const std::string& _filename)
	{
		for(uint32_t i=0; i<_binaryData.listSize; ++i)
		{
			if (_binaryData.originalFileNames[i] != _filename)
				continue;

			int size = 0;
			const auto res = _binaryData.getNamedResourceFunc(_binaryData.namedResourceList[i], size);
			return {std::make_pair(res, static_cast<uint32_t>(size))};
		}
		return {};
	}

	std::optional<std::pair<const char*, uint32_t>> Processor::findResource(const std::string& _filename) const
	{
		return findResource(m_properties.binaryData, _filename);
	}

	std::string Processor::getDataFolder(const bool _useFxFolder) const
	{
		const auto& folderName = !_useFxFolder && !getProperties().dataFolderName.empty()
			? getProperties().dataFolderName : getProductName(_useFxFolder);
		return Tools::getPublicDataFolder(getProperties().vendor, folderName);
	}

	std::string Processor::getPublicRomFolder() const
	{
		return baseLib::filesystem::validatePath(getDataFolder() + "roms/");
	}

	std::string Processor::getConfigFolder(const bool _useFxFolder) const
	{
		return baseLib::filesystem::validatePath(getDataFolder(_useFxFolder) + "config/");
	}

	std::string Processor::getPatchManagerDataFolder(bool _useFxFolder) const
	{
		return baseLib::filesystem::validatePath(getDataFolder(_useFxFolder) + "patchmanager/");
	}

	std::string Processor::getConfigFile(const bool _useFxFolder) const
	{
		return getConfigFolder(_useFxFolder) + getProductName(_useFxFolder) + ".xml";
	}

	std::string Processor::getProductName(const bool _useFxName) const
	{
		const auto& p = getProperties();
		auto name = p.name;
		if(!_useFxName && !p.isSynth && name.substr(name.size()-2, 2) == "FX")
			return name.substr(0, name.size() - 2);
		return name;
	}

	void Processor::saveDefaultMidiLearnPreset()
	{
		if (!m_midiLearnTranslator)
			return;

		MidiLearnManager manager{juce::File(getMidiLearnFolder())};
		manager.savePreset("__default", m_midiLearnTranslator->getPreset());
	}

	void Processor::loadDefaultMidiLearnPreset()
	{
		if (!m_midiLearnTranslator)
			return;

		MidiLearnManager manager{juce::File(getMidiLearnFolder())};
		MidiLearnPreset preset;

		if (manager.loadPreset("__default", preset))
			m_midiLearnTranslator->setPreset(preset);
	}

	std::string Processor::getMidiLearnFolder() const
	{
		return getConfigFolder() + "midilearn";
	}

	void Processor::getPluginDesc(bridgeLib::PluginDesc& _desc) const
	{
		_desc.plugin4CC = getProperties().plugin4CC;
		_desc.pluginName = getProperties().name;
		_desc.pluginVersion = Version::getVersionNumber();
		_desc.sessionId = m_remoteSessionId;
	}

	void Processor::setDeviceType(const DeviceType _type, const bool _forceChange/* = false*/)
	{
		if(m_deviceType == _type && !_forceChange)
			return;

		try
		{
			if(auto* dev = createDevice(_type))
			{
				getPlugin().setDevice(dev);
				(void)m_device.release();
				m_device.reset(dev);
				m_deviceType = _type;
				requestLatencyUpdate();
			}
		}
		catch(synthLib::DeviceException& e)
		{
			genericUI::MessageBox::showOk(genericUI::MessageBox::Icon::Warning,
				getName().toStdString() + " - Failed to switch device type",
				std::string("Failed to create device:\n\n") + 
				e.what() + "\n\n");
		}

		if(_type != DeviceType::Remote)
			m_remoteSessionId = generateRemoteSessionId();
	}

	void Processor::setRemoteDevice(const std::string& _host, const uint32_t _port)
	{
		if(m_remotePort == _port && m_remoteHost == _host && m_deviceType == DeviceType::Remote)
			return;

		m_remoteHost = _host;
		m_remotePort = _port;
		setDeviceType(DeviceType::Remote, true);
	}

	void Processor::destroyController()
	{
		m_midiLearnTranslator.reset();
		m_controller.reset();
	}

	//==============================================================================
	void Processor::prepareToPlay(double sampleRate, int samplesPerBlock)
	{
		// Use this method as the place to do any pre-playback
		// initialisation that you need
		m_hostSamplerate = static_cast<float>(sampleRate);

		getPlugin().setActiveOutputChannelCount(getActiveLogicalOutputChannelCount());
		getPlugin().setHostSamplerate(static_cast<float>(sampleRate), m_preferredDeviceSamplerate);
		getPlugin().setBlockSize(samplesPerBlock);
		getPlugin().reserveMidiEventCapacity(
			synthLib::Plugin::RealtimeMidiEventCapacity * 4);
		getController().prepareRealtimeMidiIngress(synthLib::Plugin::RealtimeMidiEventCapacity);
		m_midiOut.reserve(synthLib::Plugin::RealtimeMidiEventCapacity);
		{
			const std::scoped_lock lock(m_hostFeedbackMutex);
			m_hostFeedbackQueue.reserve(synthLib::Plugin::RealtimeMidiEventCapacity);
		}

		updateLatencySamples();
	}

	void Processor::releaseResources()
	{
		// When playback stops, you can use this as an opportunity to free up any
		// spare memory, etc.
	}

	bool Processor::isBusesLayoutSupported(const BusesLayout& _busesLayout) const
	{
	    // This is the place where you check if the layout is supported.
	    // In this template code we only support mono or stereo.
	    // Some plugin hosts, such as certain GarageBand versions, will only
	    // load plugins that support stereo bus layouts.
	    if (_busesLayout.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
	        return false;

	    // This checks if the input is stereo
	    if (_busesLayout.getMainInputChannelSet() != juce::AudioChannelSet::stereo())
	        return false;

	    return true;
	}

	//==============================================================================
	void Processor::getStateInformation (juce::MemoryBlock& destData)
	{
	    // You should use this method to store your parameters in the memory block.
	    // You could do that either as raw data, or use the XML or ValueTree classes
	    // as intermediaries to make it easy to save and load complex data.
#if !SYNTHLIB_DEMO_MODE
		PluginStream ss;
		ss.write(g_saveMagic);
		ss.write(g_saveVersion);
		std::vector<uint8_t> buffer;
		saveCustomData(buffer);
		ss.write(buffer);

		std::vector<uint8_t> buf;
		ss.toVector(buf);

		destData.append(buf.data(), buf.size());
#endif
	}

	void Processor::setStateInformation (const void* _data, const int _sizeInBytes)
	{
#if !SYNTHLIB_DEMO_MODE
		// You should use this method to restore your parameters from this memory block,
	    // whose contents will have been created by the getStateInformation() call.
		setState(_data, _sizeInBytes);
#endif
	}

	void Processor::getCurrentProgramStateInformation(juce::MemoryBlock& destData)
	{
#if !SYNTHLIB_DEMO_MODE
		std::vector<uint8_t> state;
		getPlugin().getState(state, synthLib::StateTypeCurrentProgram);
		destData.append(state.data(), state.size());
#endif
	}

	void Processor::setCurrentProgramStateInformation(const void* data, int sizeInBytes)
	{
#if !SYNTHLIB_DEMO_MODE
		setState(data, sizeInBytes);
#endif
	}
		
	const juce::String Processor::getName() const
	{
	    return getProperties().name;
	}

	bool Processor::acceptsMidi() const
	{
		return getProperties().wantsMidiInput;
	}

	bool Processor::producesMidi() const
	{
		return getProperties().producesMidiOut;
	}

	bool Processor::isMidiEffect() const
	{
		return getProperties().isMidiEffect;
	}

	void Processor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
	{
	    juce::ScopedNoDenormals noDenormals;
	    const int numSamples = buffer.getNumSamples();
		synthLib::RealtimeInstrumentation::CallbackScope instrumentation(
			getPlugin().getRealtimeInstrumentation(), static_cast<size_t>(numSamples),
			getSampleRate());

	    synthLib::TAudioInputs inputs{};
	    synthLib::TAudioOutputs outputs{};

		// Preserve declared logical channel positions when an intermediate bus is
		// disabled. JUCE flattens only enabled buses into the processBlock buffer;
		// treating that buffer as one contiguous device layout would route E/F into
		// C/D whenever the C/D bus is disabled.
		size_t fallbackInputChannel = 0;
		for(int busIndex = 0; busIndex < getBusCount(true); ++busIndex)
		{
			auto busBuffer = getBusBuffer(buffer, true, busIndex);
			const auto logicalInputChannel = static_cast<size_t>(busIndex)
				< getProperties().logicalInputBusOffsets.size()
				? getProperties().logicalInputBusOffsets[static_cast<size_t>(busIndex)]
				: fallbackInputChannel;
			for(int channel = 0; channel < busBuffer.getNumChannels(); ++channel)
			{
				const auto logical = logicalInputChannel
					+ static_cast<size_t>(channel);
				if(logical < inputs.size())
					inputs[logical] = busBuffer.getReadPointer(channel);
			}
			fallbackInputChannel += static_cast<size_t>(busBuffer.getNumChannels());
		}

		size_t fallbackOutputChannel = 0;
		for(int busIndex = 0; busIndex < getBusCount(false); ++busIndex)
		{
			auto busBuffer = getBusBuffer(buffer, false, busIndex);
			const auto logicalOutputChannel = static_cast<size_t>(busIndex)
				< getProperties().logicalOutputBusOffsets.size()
				? getProperties().logicalOutputBusOffsets[static_cast<size_t>(busIndex)]
				: fallbackOutputChannel;
			for(int channel = 0; channel < busBuffer.getNumChannels(); ++channel)
			{
				const auto logical = logicalOutputChannel
					+ static_cast<size_t>(channel);
				if(logical < outputs.size())
					outputs[logical] = busBuffer.getWritePointer(channel);
			}
			fallbackOutputChannel += static_cast<size_t>(busBuffer.getNumChannels());
		}
		if(instrumentation.isActive())
		{
			uint32_t activeOutputBuses = 0;
			uint32_t activeOutputChannels = 0;
			for(int busIndex = 0; busIndex < getBusCount(false); ++busIndex)
			{
				const auto busBuffer = getBusBuffer(buffer, false, busIndex);
				if(busBuffer.getNumChannels() > 0)
				{
					++activeOutputBuses;
					activeOutputChannels += static_cast<uint32_t>(
						busBuffer.getNumChannels());
				}
			}
			instrumentation.setActiveOutputLayout(activeOutputBuses,
				activeOutputChannels);
		}

		uint32_t diagnosticMidiEvents = 0, diagnosticMidiBytes = 0;
		for(const auto metadata : midiMessages)
		{
			if(instrumentation.isActive() && metadata.numBytes > 0)
			{
				++diagnosticMidiEvents;
				diagnosticMidiBytes += static_cast<uint32_t>(metadata.numBytes);
			}
			if(metadata.numBytes <= 0)
				continue;
			// SMidiEvent owns SysEx bytes. On older supported Apple deployment
			// targets that storage is std::vector, and larger packets may also
			// exceed the inline PMR storage elsewhere. Keep the normal channel-
			// message path allocation-free and explicitly account for every visit
			// to the arbitrary-size fallback path.
			if(metadata.numBytes > 3 || metadata.data[0] == synthLib::M_STARTOFSYSEX)
				++m_realtimeMidiAllocationFallbackCount;
			synthLib::SMidiEvent ev(synthLib::MidiEventSource::Host);
			if(ev.assignRawData(metadata.data, static_cast<size_t>(metadata.numBytes),
				synthLib::MidiEventSource::Host,
				static_cast<uint32_t>(std::max(0, metadata.samplePosition))))
				addMidiEvent(ev);
		}

		midiMessages.clear();

		getController().processRealtimeParameterChanges(
			synthLib::Plugin::RealtimeMidiEventCapacity);

		bool isPlaying = true;
		bool transportKnown = false;
		double bpm = 0.0;
		double ppqPos = 0.0;
		bool ppqKnown = false;

	    if(const auto* playHead = getPlayHead())
		{
			if(auto pos = playHead->getPosition())
			{
				isPlaying = pos->getIsPlaying();
				transportKnown = true;

				if(pos->getBpm())
				{
					bpm = *pos->getBpm();
					processBpm(static_cast<float>(bpm));
				}
				if(pos->getPpqPosition())
				{
					ppqPos = *pos->getPpqPosition();
					ppqKnown = true;
				}
			}
		}

		instrumentation.setHostState(isPlaying, isNonRealtime(), transportKnown);
		instrumentation.setMidiInputSummary(diagnosticMidiEvents, diagnosticMidiBytes);
		getPlugin().process(inputs, outputs, numSamples, bpm, ppqPos, isPlaying, ppqKnown);

		applyOutputGain(outputs, numSamples);

		m_midiOut.clear();
		getPlugin().getMidiOut(m_midiOut);

	    for (auto& e : m_midiOut)
	    {
		    addMidiEvent(e);

			if (!getMidiRoutingMatrix().enabled(e, synthLib::MidiEventSource::Host))
			    continue;

	    	const auto mm = MidiPorts::toJuceMidiMessage(e);
		    midiMessages.addEvent(mm, static_cast<int>(e.offset));
	    }

		// Drain MIDI Learn feedback events destined for the host
		{
			const std::scoped_lock lock(m_hostFeedbackMutex);
			for (const auto& e : m_hostFeedbackQueue)
			{
				const auto mm = MidiPorts::toJuceMidiMessage(e);
				midiMessages.addEvent(mm, 0);
			}
			m_hostFeedbackQueue.clear();
		}

		// Offline hosts are allowed to render faster than wall clock and may not run
		// a JUCE message loop between blocks. Service controller MIDI here only when
		// the host has explicitly declared this callback non-realtime. The realtime
		// path remains bounded and free of this lock/parsing work.
		if(isNonRealtime())
			getController().processOfflineControllerWork();
	}

	void Processor::processBlockBypassed(juce::AudioBuffer<float>& _buffer, juce::MidiBuffer& _midiMessages)
	{
		synthLib::RealtimeInstrumentation::CallbackScope instrumentation(
			getPlugin().getRealtimeInstrumentation(),
			static_cast<size_t>(_buffer.getNumSamples()), getSampleRate(), true);
		bool transportPlaying = false, transportKnown = false;
		if(instrumentation.isActive())
			if(const auto* playHead = getPlayHead())
				if(auto position = playHead->getPosition())
				{
					transportPlaying = position->getIsPlaying();
					transportKnown = true;
				}
		instrumentation.setHostState(transportPlaying, isNonRealtime(), transportKnown);
		if(instrumentation.isActive())
		{
			uint32_t activeOutputBuses = 0;
			uint32_t activeOutputChannels = 0;
			for(int busIndex = 0; busIndex < getBusCount(false); ++busIndex)
			{
				const auto* const bus = getBus(false, busIndex);
				if(bus && bus->isEnabled())
				{
					++activeOutputBuses;
					activeOutputChannels += static_cast<uint32_t>(
						bus->getNumberOfChannels());
				}
			}
			instrumentation.setActiveOutputLayout(activeOutputBuses,
				activeOutputChannels);
		}

		if(getProperties().isSynth || getTotalNumInputChannels() <= 0)
		{
			_buffer.clear(0, _buffer.getNumSamples());
			return;
		}

		const auto sampleCount = static_cast<uint32_t>(_buffer.getNumSamples());
		const auto outCount = static_cast<uint32_t>(getTotalNumOutputChannels());
		const auto inCount = static_cast<uint32_t>(getTotalNumInputChannels());

		uint32_t inCh = 0;

		for(uint32_t outCh=0; outCh<outCount; ++outCh)
		{
			auto* input = _buffer.getReadPointer(static_cast<int>(inCh));
			auto* output = _buffer.getWritePointer(static_cast<int>(outCh));

			m_bypassBuffer.write(input, outCh, sampleCount, getLatencySamples());
			m_bypassBuffer.read(output, outCh, sampleCount);

			++inCh;

			if(inCh >= inCount)
				inCh = 0;
		}

//		AudioProcessor::processBlockBypassed(_buffer, _midiMessages);
	}

	uint32_t Processor::getActiveLogicalOutputChannelCount() const
	{
		uint32_t highest = 0;
		size_t fallbackChannel = 0;
		for(int busIndex = 0; busIndex < getBusCount(false); ++busIndex)
		{
			const auto* const bus = getBus(false, busIndex);
			const auto channels = bus && bus->isEnabled()
				? static_cast<uint32_t>(bus->getNumberOfChannels()) : 0u;
			const auto logical = static_cast<size_t>(busIndex)
				< getProperties().logicalOutputBusOffsets.size()
				? getProperties().logicalOutputBusOffsets[static_cast<size_t>(busIndex)]
				: fallbackChannel;
			if(channels)
				highest = std::max(highest, static_cast<uint32_t>(logical) + channels);
			if(bus)
				fallbackChannel += static_cast<size_t>(bus->getNumberOfChannels());
		}
		return highest;
	}

	void Processor::numChannelsChanged()
	{
		if(m_plugin)
			getPlugin().setActiveOutputChannelCount(getActiveLogicalOutputChannelCount());
		requestLatencyUpdate();
	}

#if !SYNTHLIB_DEMO_MODE
	void Processor::setState(const void* _data, const size_t _sizeInBytes)
	{
		if(_sizeInBytes < 1)
			return;

		std::vector<uint8_t> state;
		state.resize(_sizeInBytes);
		memcpy(state.data(), _data, _sizeInBytes);

		try
		{
			PluginStream ss(state);

			if (ss.checkString(g_saveMagic))
			{
				const std::string magic = ss.readString();

				if (magic != g_saveMagic)
					return;

				const auto version = ss.read<uint32_t>();

				if (version > g_saveVersion)
					return;

				std::vector<uint8_t> buffer;

				if(version == 1)
				{
					ss.read(buffer);
					getPlugin().setState(buffer);
				}

				ss.read(buffer);

				if(!buffer.empty())
				{
					try
					{
						loadCustomData(buffer);
					}
					catch (std::range_error&)
					{
					}
				}
			}
			else
			{
				getPlugin().setState(state);
			}
		}
		catch (std::range_error& e)
		{
			LOG("Failed to read state: " << e.what());
			return;
		}

		if (hasController())
			getController().onStateLoaded();
	}
#endif

	//==============================================================================

	int Processor::getNumPrograms()
	{
		return 1; // NB: some hosts don't cope very well if you tell them there are 0 programs,
				  // so this should be at least 1, even if you're not really implementing programs.
	}

	int Processor::getCurrentProgram()
	{
		return 0;
	}

	void Processor::setCurrentProgram(int _index)
	{
		juce::ignoreUnused(_index);
	}

	void Processor::notifyHostOfProgramChange()
	{
		// Preset loads update many parameters without notifying the host one by one.
		// Publishing their current values here also refreshes JUCE's VST3 parameter
		// cache, avoiding wrapper-specific behavior in every controller.
		for(auto* const parameter : getParameters())
			parameter->sendValueChangedMessageToListeners(parameter->getValue());

		updateHostDisplay(juce::AudioProcessorListener::ChangeDetails()
			.withProgramChanged(true));
	}

	const juce::String Processor::getProgramName(int _index)
	{
		juce::ignoreUnused(_index);
		return m_programName;
	}

	void Processor::addHostMidiFeedback(const synthLib::SMidiEvent& _event)
	{
		const std::scoped_lock lock(m_hostFeedbackMutex);
		m_hostFeedbackQueue.push_back(_event);
	}

	void Processor::changeProgramName(int _index, const juce::String& _newName)
	{
		m_programName = _newName.toStdString();
	}

	double Processor::getTailLengthSeconds() const
	{
		return 0.0f;
	}

	void Processor::onDeviceInvalid(synthLib::Device* _device)
	{
		// This callback is invoked while Plugin::process holds its audio lock. Device
		// creation, state transfer, resampler setup, UI work, and heap allocation are
		// therefore deferred to handleAsyncUpdate on the message thread. Plugin aborts
		// this block immediately after the notification without reconfiguration.
		if(!m_deviceRecoveryPending.exchange(true))
			triggerAsyncUpdate();
	}

	void Processor::recoverInvalidDevice()
	{
		bool recovered = false;
		if(m_deviceType == DeviceType::Remote)
		{
			try
			{
				// attempt one reconnect
				std::unique_ptr<synthLib::Device> newDevice(createRemoteDevice());
				if(newDevice && newDevice->isValid())
				{
					getPlugin().setDevice(newDevice.get());
					(void)m_device.release();
					m_device = std::move(newDevice);
					recovered = true;
				}
			}
			catch (synthLib::DeviceException& e)
			{
				juce::MessageManager::callAsync([e]
				{
					genericUI::MessageBox::showOk(genericUI::MessageBox::Icon::Warning,
						"Device creation failed:",
						std::string("The connection to the remote server has been lost and a reconnect failed. Processing mode has been switched to local processing\n\n") + 
						e.what() + "\n\n");
				});
			}
		}

		// Force this even if the failed device was already local. The previous code's
		// ordinary Local -> Local transition was a no-op and left it invalid forever.
		if(!recovered)
			setDeviceType(DeviceType::Local, true);

		if(hasController())
			getController().onStateLoaded();
	}

	bool Processor::rebootDevice()
	{
		try
		{
			synthLib::Device* device = createDevice();
			getPlugin().setDevice(device);
			(void)m_device.release();
			m_device.reset(device);
			requestLatencyUpdate();

			return true;
		}
		catch(const synthLib::DeviceException& e)
		{
			genericUI::MessageBox::showOk(genericUI::MessageBox::Icon::Warning,
				"Device creation failed:",
				std::string("Failed to create device:\n\n") + 
				e.what() + "\n\n");
			return false;
		}
	}
}
