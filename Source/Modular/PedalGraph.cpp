#include "Modular/PedalGraph.h"

namespace pb
{
CompileResult compile (const PedalGraph& graph, std::unique_ptr<PedalRenderPlan>& out,
                       double sampleRate, int maxBlockSize)
{
    // out is left untouched: PedalRenderPlan is only forward declared here, so touching
    // the unique_ptr would need its complete type. Card 6.2 defines it and fills this in.
    juce::ignoreUnused (graph, out, sampleRate, maxBlockSize);

    // TODO card 6.2
    return { false, "custom pedals are not implemented yet (card 6.2)" };
}
} // namespace pb
