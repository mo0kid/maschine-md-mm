#pragma once

#include "jucePluginEditorLib/pluginProcessor.h"
#include "mdLib/mdtypes.h"
#include "synthLib/performanceReport.h"

#include <optional>
#include <memory>
#include <string>
#include <string_view>
#include <mutex>
#include <vector>

namespace md
{
	class FrontPanelPublisher;
}

namespace mdJucePlugin
{
	class AudioPluginAudioProcessor : public jucePluginEditorLib::Processor,
		private juce::Timer
	{
	public:
		struct EphemeralConfig final
		{
			// Tests may explicitly isolate the emulated machine from persistent
			// factory/storage caches. A disengaged value preserves normal discovery;
			// an engaged empty value disables the device home path entirely.
			std::optional<std::string> deviceHomePath;
		};

	    AudioPluginAudioProcessor();
		explicit AudioPluginAudioProcessor(md::MachineModel _model);
		AudioPluginAudioProcessor(md::MachineModel _model, bool _allowMcpServer);
		AudioPluginAudioProcessor(md::MachineModel _model, EphemeralConfig,
			bool _allowMcpServer = false);
		AudioPluginAudioProcessor(md::MachineModel _model,
			std::vector<uint8_t> _initialPatchRam, bool _allowMcpServer = true);
	    ~AudioPluginAudioProcessor() override;

		md::MachineModel getModel() const { return m_model; }
		std::shared_ptr<md::FrontPanelPublisher> tryGetFrontPanelPublisher();
		uint8_t getCurrentPattern();
		uint16_t getDrumHitMask();
		uint8_t getMidiBaseChannel();
		uint8_t getMonomachineNoteChannel(uint8_t _track);
		bool sendPanelEvent(uint8_t _command, uint8_t _argument);
		static md::MachineModel getCompiledProductModel();
		static bool hasEmbeddedProductResource(std::string_view _filename);
		juce::File getInstalledFactoryStorageImage() const;
		juce::File getStorageRecoveryImage() const;
		bool factoryReset(juce::String& _error);
		bool loadStorageImage(const juce::File& _source, juce::String& _result);
		bool serviceFactoryInitialization();
		bool serviceProjectStateRestore();
		std::string getProjectStateRestoreError();
		void setPerformanceDiagnosticsEnabled(bool _enabled);
		bool performanceDiagnosticsActive() const;
		std::string performanceDiagnosticsStatus() const;
		juce::File performanceDiagnosticsFolder() const;
		juce::File performanceDiagnosticsFile() const { return m_performanceReportFile; }

	    jucePluginEditorLib::PluginEditorState* createEditorState() override;
	    synthLib::Device* createDevice() override;
		void getRemoteDeviceParams(synthLib::DeviceCreateParams& _params) const override;

	    pluginLib::Controller* createController() override;
		void saveChunkData(baseLib::BinaryStream& _stream) override;
		void loadChunkData(baseLib::ChunkReader& _reader) override;

	private:
		static BusesProperties createBusesProperties();
		bool isBusesLayoutSupported(const BusesLayout& _layout) const override;
		AudioPluginAudioProcessor(md::MachineModel _model,
			std::vector<uint8_t> _initialPatchRam, bool _allowMcpServer,
			bool _ephemeralConfig,
			std::optional<std::string> _deviceHomePath = std::nullopt);
		bool serviceDeferredStateRestore();
		bool serviceStateRestoreFailure();
		void recordStandaloneStartupDiagnostics();
		void reportProjectStateRestoreFailure(const std::string& _error);
		void timerCallback() override;

		std::unique_ptr<synthLib::PerformanceReport> m_performanceReport;
		juce::File m_performanceReportFile;
		bool m_performanceFolderError = false;
		const md::MachineModel m_model;
		const std::vector<uint8_t> m_initialPatchRam;
		const std::optional<std::string> m_deviceHomePath;
		// Access with atomic shared_ptr operations: the UI and Maschine reader
		// can both acquire this while the boot/audio worker owns the device lock.
		std::shared_ptr<md::FrontPanelPublisher> m_frontPanelPublisher;
		std::mutex m_storageLoadMutex;
		uint64_t m_reportedRestoreFailureGeneration = 0;
		juce::File m_startupDiagnosticsFile;
		double m_startupDiagnosticsStartMilliseconds = 0.0;
		bool m_startupDiagnosticsEnabled = false;
		JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioPluginAudioProcessor)
	};
}
