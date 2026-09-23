#pragma once

#include "mdMaschineController.h"
#include "mdCombinedMidiRouter.h"
#include "mdPluginProcessor.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <thread>

namespace mdJucePlugin
{
	class CombinedProcessor final : public juce::AudioProcessor
	{
	public:
		CombinedProcessor();
		~CombinedProcessor() override;

		AudioPluginAudioProcessor& machinedrum() { return m_machinedrum; }
		AudioPluginAudioProcessor& monomachine() { return m_monomachine; }

		void prepareToPlay(double _sampleRate, int _maximumBlockSize) override;
		void releaseResources() override;
		void processBlock(juce::AudioBuffer<float>& _audio,
			juce::MidiBuffer& _midi) override;
		bool isBusesLayoutSupported(const BusesLayout& _layouts) const override;

		juce::AudioProcessorEditor* createEditor() override;
		bool hasEditor() const override { return true; }
		const juce::String getName() const override { return "Maschine MD-MM"; }
		bool acceptsMidi() const override { return true; }
		bool producesMidi() const override { return true; }
		bool isMidiEffect() const override { return false; }
		double getTailLengthSeconds() const override { return 0.0; }

		int getNumPrograms() override { return 1; }
		int getCurrentProgram() override { return 0; }
		void setCurrentProgram(int) override {}
		const juce::String getProgramName(int) override { return {}; }
		void changeProgramName(int, const juce::String&) override {}
		void getStateInformation(juce::MemoryBlock& _destination) override;
		void setStateInformation(const void* _data, int _size) override;

	private:
		void runMonomachineWorker();
		void runFastBoot(AudioPluginAudioProcessor& _processor,
			std::atomic<bool>& _active);
		void stopFastBootWorkers();

		AudioPluginAudioProcessor m_machinedrum;
		AudioPluginAudioProcessor m_monomachine;
		maschine::Controller m_maschine;
		juce::AudioBuffer<float> m_mdAudio;
		juce::AudioBuffer<float> m_mmAudio;
		juce::MidiBuffer m_mdMidi;
		juce::MidiBuffer m_mmMidi;
		CombinedMidiRouter m_midiRouter;
		juce::WaitableEvent m_mmWorkReady;
		juce::WaitableEvent m_mmWorkFinished;
		std::atomic<bool> m_stoppingWorker{false};
		std::thread m_mmWorker;
		std::atomic<bool> m_stopFastBoot{false};
		std::atomic<bool> m_mdFastBootActive{false};
		std::atomic<bool> m_mmFastBootActive{false};
		std::thread m_mdFastBootWorker;
		std::thread m_mmFastBootWorker;
		int m_maximumBlockSize = 0;

		JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CombinedProcessor)
	};
}
