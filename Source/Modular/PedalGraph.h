#pragma once
// SCAFFOLD — fixed interface for Phase 6. Do not change without review.
#include "Effects/IEffect.h"
#include <vector>

namespace pb
{
// ---- Editable model (message thread) ----
struct PedalNode
{
    int id = 0;                    // unique within the pedal
    juce::String typeId;           // any EffectRegistry id, or "node.in", "node.out",
                                   // "node.split", "node.mix", "node.gain", "node.filter",
                                   // "node.lfo", "node.envfollow", "node.fbsend", "node.fbreturn"
    std::vector<float> paramValues;
    float x = 0, y = 0;            // patcher position
    int feedbackPairId = 0;        // fbsend and fbreturn with the same pairId are linked
};

struct PedalWire
{
    int fromNode = 0, fromPort = 0;
    int toNode = 0, toPort = 0;
    bool isModulation = false;     // LFO / env follower to a param: toPort = param index
};

struct PedalKnob                  // a knob on the pedal's face
{
    juce::String name;
    int nodeId = 0, paramIndex = 0;
    float minValue = 0, maxValue = 1;
};

struct PedalGraph
{
    juce::String name;
    std::vector<PedalNode> nodes;
    std::vector<PedalWire> wires;
    std::vector<PedalKnob> knobs; // up to 8
};

// ---- Compiled form (built on message thread, run on audio thread) ----
struct CompileResult
{
    bool ok = false;
    juce::String error;            // e.g. "Cycle between Delay 2 and Filter 1 without a feedback pair"
};

// Topologically sorts audio wires (feedback pairs break cycles), assigns a buffer per
// audio port, and produces a flat run list. Implemented in card 6.2.
class PedalRenderPlan;
CompileResult compile (const PedalGraph& graph, std::unique_ptr<PedalRenderPlan>& out,
                       double sampleRate, int maxBlockSize);
} // namespace pb
