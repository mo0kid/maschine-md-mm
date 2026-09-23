#include "mdPluginProcessor.h"

#if defined(MD_JUCEPLUGIN_COMBINED)
#include "mdCombinedProcessor.h"
#endif

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
	#if defined(MD_JUCEPLUGIN_COMBINED)
	return new mdJucePlugin::CombinedProcessor();
	#else
	return new mdJucePlugin::AudioPluginAudioProcessor();
	#endif
}
