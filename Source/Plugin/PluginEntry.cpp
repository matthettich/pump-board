#include "Plugin/PluginProcessor.h"

// Card 8.2: the stub from card 8.1 is gone; the bundle now carries the board. The entry point stays this small on
// purpose: everything worth testing lives in PluginProcessor and PluginSession, which the test target links too.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new pb::PluginProcessor();
}
