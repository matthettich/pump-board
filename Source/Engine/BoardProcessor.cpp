#include "Control/MidiClock.h"
#include "Engine/BoardProcessor.h"
#include "Common/Sanitize.h"
#include "Effects/TempoAware.h"
#include "Engine/SafetyLimiter.h"
#include "Common/AllocationGuard.h"
#include "Control/MidiRouter.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace pb
{
namespace
{
constexpr double kTrimRampSeconds = 0.02;
constexpr double kBypassRampSeconds = 0.010;      // the kit's 10 ms equal-power crossfade

// ParameterRegistry::get() does no bounds check, so no kInvalid index may reach it.
float safeGet (const ParameterRegistry& registry, int index, float fallback) noexcept
{
    return index == ParameterRegistry::kInvalid ? fallback : registry.get (index);
}
} // namespace

BoardProcessor::BoardProcessor (ParameterRegistry& registryIn, MidiRouter& routerIn)
    : juce::AudioProcessor (BusesProperties()
          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      registry (registryIn),
      midiRouter (routerIn)
{
    // The bus layout is declared here so AudioProcessorPlayer can set the channel counts the device offers;
    // the scaffold ctor took no BusesProperties (see docs/NOTES.md).

    ParameterRegistry::Entry entry;
    entry.id = "board/bypass";
    entry.displayName = "Board Bypass";
    entry.minValue = 0.0f;
    entry.maxValue = 1.0f;
    entry.defaultValue = 0.0f;
    entry.isToggle = true;
    bypassIndex = registry.add (entry);

    entry = {};
    entry.id = "board/inputTrim";
    entry.displayName = "Input Trim";
    entry.minValue = -24.0f;
    entry.maxValue = 12.0f;
    entry.defaultValue = 0.0f;
    inputTrimIndex = registry.add (entry);

    entry = {};
    entry.id = "board/outputTrim";
    entry.displayName = "Output Trim";
    entry.minValue = -24.0f;
    entry.maxValue = 12.0f;
    entry.defaultValue = 0.0f;
    outputTrimIndex = registry.add (entry);

    // The board's tempo. Synced echo times are the first thing that follows it.
    entry = {};
    entry.id = "board/tempo";
    entry.displayName = "Tempo";
    entry.minValue = 20.0f;
    entry.maxValue = 300.0f;
    entry.defaultValue = 120.0f;
    tempoIndex = registry.add (entry);
}

BoardProcessor::~BoardProcessor()
{
    delete active.exchange (nullptr, std::memory_order_acquire);

    for (auto& retired : retireList)
        delete retired.snapshot;

    retireList.clear();
}

void BoardProcessor::publish (std::unique_ptr<ChainSnapshot> next)
{
    auto* incoming = next.release();
    auto* previous = active.exchange (incoming, std::memory_order_acq_rel);
    publishCount.fetch_add (1, std::memory_order_release);

    if (previous != nullptr)
        retireList.push_back ({ previous, blocksProcessed.load (std::memory_order_acquire) });
}

bool BoardProcessor::detachChain (int timeoutMs)
{
    publish (std::make_unique<ChainSnapshot>());

    if (! deviceRunning.load (std::memory_order_acquire))
        return true;

    // The same two-block rule as collectGarbage: once the counter has moved twice, the block that might have been
    // running the old chain when it was swapped out has finished.
    const auto start = blocksProcessed.load (std::memory_order_acquire);
    const auto deadline = juce::Time::getMillisecondCounter() + (juce::uint32) juce::jmax (1, timeoutMs);

    while (blocksProcessed.load (std::memory_order_acquire) < start + 2)
    {
        if (juce::Time::getMillisecondCounter() >= deadline)
            return false;

        juce::Thread::sleep (1);
    }

    return true;
}

void BoardProcessor::collectGarbage()
{
    // A snapshot retired during block N can still be in use until block N+1 ends, so it goes at N+2.
    auto now = blocksProcessed.load (std::memory_order_acquire);

    // The counter only moves while the device calls back. With the device stopped (releaseResources has run, which
    // the player does under the lock the audio callback takes) or never started, no block can be reading anything,
    // and waiting for two more would keep every retired slot - and the params it holds - alive for good.
    if (! deviceRunning.load (std::memory_order_acquire))
        now = std::numeric_limits<uint64_t>::max() - 2;

    retireList.erase (std::remove_if (retireList.begin(), retireList.end(),
                                      [now] (const Retired& retired)
                                      {
                                          if (retired.retiredAtBlock + 2 <= now)
                                          {
                                              delete retired.snapshot;
                                              return true;
                                          }

                                          return false;
                                      }),
                      retireList.end());
}

int BoardProcessor::maxDryDelayFor (double sampleRate) noexcept
{
    return juce::jmax (16384, 4 * Slot::maxDelayFor (sampleRate));
}

void BoardProcessor::prepareToPlay (double newSampleRate, int maximumExpectedSamplesPerBlock)
{
    sampleRateHz = newSampleRate > 0.0 ? newSampleRate : 48000.0;
    maxBlock = juce::jmax (16, maximumExpectedSamplesPerBlock);

    inGain.reset (sampleRateHz, kTrimRampSeconds);
    outGain.reset (sampleRateHz, kTrimRampSeconds);
    boardWet.reset (sampleRateHz, kBypassRampSeconds);

    inGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (safeGet (registry, inputTrimIndex, 0.0f)));
    outGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (safeGet (registry, outputTrimIndex, 0.0f)));
    boardWet.setCurrentAndTargetValue (safeGet (registry, bypassIndex, 0.0f) >= 0.5f ? 0.0f : 1.0f);
    lastTempo = -1.0;                                // a fresh device gets the tempo, whatever it was before

    // Card 5.4: the dry path is a delay ring, one block plus the deepest latency a board can hold (16384 frames is
    // 340 ms at 48k — COMP's 20 ms of lookahead at 4x oversampling and CALM's 8192-point FFT both fit).
    maxDryDelay = maxDryDelayFor (sampleRateHz);
    dryBuffer.setSize (2, maxBlock + maxDryDelay, false, true, false);
    dryBuffer.clear();
    dryWritePos = 0;

    limiter.prepare (sampleRateHz);

    inPeak.store (0.0f, std::memory_order_relaxed);
    outPeak.store (0.0f, std::memory_order_relaxed);

    deviceRunning.store (true, std::memory_order_release);
}

void BoardProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    // Card 2.4: anything that allocates from here down is counted in Debug builds and must stay at zero.
    [[maybe_unused]] const pb::alloc::Scope allocationScope;   // empty without PB_ALLOCATION_GUARD

    // The publish counter first: publish() swaps `active` before it bumps the counter, so a new count read here
    // guarantees the load below sees the new chain. Read the other way round, a publish landing between the two loads
    // pushed the tempo to the old chain and marked the new one as served (CodeRabbit, PR #1).
    const auto published = publishCount.load (std::memory_order_acquire);
    const ChainSnapshot* snapshot = active.load (std::memory_order_acquire);
    // Release, not relaxed: the message thread's acquire load of this counter is what proves the previous block's
    // reads of an old snapshot are finished (on Apple silicon a relaxed increment does not order them).
    blocksProcessed.fetch_add (1, std::memory_order_acq_rel);

    midiRouter.handle (midi);

    // Card 5.32: a running MIDI clock moves `board/tempo`, here and before the tempo is handed to the pedals, so a
    // clock change reaches them on the same block. Allocation-free: the index is cached inside the clock.
    midiClock.observe (midi, buffer.getNumSamples(), registry, getSampleRate(), tempoIndex);

    // The board's tempo, handed to the pedals that follow it. Only when it changes, so a steady tempo is free.
    // The cast happens here and only here, on a tempo change rather than every block.
    if (snapshot != nullptr)
    {
        const double bpm = (double) safeGet (registry, tempoIndex, 120.0f);

        // A change test, not an equality test: a tempo that moved by less than a hundredth of a BPM is not a
        // change, and saying so with a difference keeps the audio thread honest about float comparison.
        // A new chain (a pedal inserted, swapped or a board loaded) gets the tempo too: a tempo-aware pedal starts
        // at its own 120 BPM and would otherwise stay there until the tempo next moved.
        // (A counter rather than the pointer: a new snapshot can be allocated where a freed one used to be.)
        if (published != lastTempoPublish)
        {
            lastTempoPublish = published;
            lastTempo = -1.0;
        }

        const double delta = bpm - lastTempo;

        if (delta > 0.001 || delta < -0.001)
        {
            lastTempo = bpm;

            for (int i = 0; i < snapshot->count; ++i)
                if (auto* slot = snapshot->order[(size_t) i])
                    if (auto* tempoAware = dynamic_cast<ITempoAware*> (slot->getEffect()))
                        tempoAware->setTempo (bpm);
        }
    }

    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    if (numSamples <= 0 || numChannels <= 0)
        return;

    for (int ch = 2; ch < numChannels; ++ch)
        buffer.clear (ch, 0, numSamples);

    const int channels = juce::jmin (2, numChannels);

    // A non-finite sample from the device (a Mac interface's first buffer can be garbage) would poison every pedal
    // with feedback until it was rebuilt; it is zeroed before anything reads it (see Common/Sanitize.h).
    scrubNonFinite (buffer, channels, numSamples);

    // Mono input: a guitar wired to input 1 leaves channel 2 silent, so fold it up.
    // The card asks for a 1 s test; the fixed header has no counter member for that, so this is per block.
    if (channels == 2
        && buffer.getMagnitude (1, 0, numSamples) <= 1.0e-6f
        && buffer.getMagnitude (0, 0, numSamples) > 1.0e-6f)
        buffer.copyFrom (1, 0, buffer, 0, 0, numSamples);

    // The input meter reads what arrived, before the trim.
    inPeak.store (juce::jmax (buffer.getMagnitude (0, 0, numSamples),
                              channels > 1 ? buffer.getMagnitude (1, 0, numSamples) : 0.0f),
                  std::memory_order_relaxed);

    // 1. input trim
    inGain.setTargetValue (juce::Decibels::decibelsToGain (safeGet (registry, inputTrimIndex, 0.0f)));
    for (int i = 0; i < numSamples; ++i)
    {
        const float gain = inGain.getNextValue();
        for (int ch = 0; ch < channels; ++ch)
            buffer.getWritePointer (ch)[i] *= gain;
    }

    // 2. the chain. Card 5.4: the dry copy goes into a delay ring and is read back `chainLatency` frames later, so
    //    a pedal that reports latency no longer makes the board bypass a comb of two misaligned copies of the
    //    same signal. The sum is recomputed per block (at most sixteen virtual calls) because lookahead and
    //    oversampling can change it while the board is playing.
    int chainLatency = 0;

    if (snapshot != nullptr)
        for (int i = 0; i < snapshot->count; ++i)
            if (auto* slot = snapshot->order[(size_t) i])
                if (auto* fx = slot->getEffect())
                    chainLatency += fx->latencySamples();   // an off slot keeps its latency (see Slot::process)

    chainLatency = juce::jlimit (0, maxDryDelay, chainLatency);

    const int ringSize = dryBuffer.getNumSamples();
    const bool haveDryCopy = numSamples <= ringSize && numSamples + chainLatency <= ringSize;

    if (haveDryCopy)
    {
        for (int ch = 0; ch < channels; ++ch)
        {
            auto* dst = dryBuffer.getWritePointer (ch);
            const int first = juce::jmin (numSamples, ringSize - dryWritePos);

            juce::FloatVectorOperations::copy (dst + dryWritePos, buffer.getReadPointer (ch), first);

            if (first < numSamples)
                juce::FloatVectorOperations::copy (dst, buffer.getReadPointer (ch) + first, numSamples - first);
        }

        dryWritePos = (dryWritePos + numSamples) % ringSize;
    }

    if (snapshot != nullptr)
        for (int i = 0; i < snapshot->count; ++i)
            if (auto* slot = snapshot->order[(size_t) i])
                slot->process (buffer);

    // 3. output trim, then the board bypass crossfade. The trim is applied to both paths so bypassing the
    //    board does not change the level.
    outGain.setTargetValue (juce::Decibels::decibelsToGain (safeGet (registry, outputTrimIndex, 0.0f)));
    boardWet.setTargetValue (safeGet (registry, bypassIndex, 0.0f) >= 0.5f ? 0.0f : 1.0f);

    if (haveDryCopy)
    {
        // (dryWritePos has already advanced past this block, so the block's own first sample sits at -numSamples)
        int readPos = ((dryWritePos - numSamples - chainLatency) % ringSize + ringSize) % ringSize;

        for (int i = 0; i < numSamples; ++i)
        {
            const float trim = outGain.getNextValue();
            const float wet = juce::jlimit (0.0f, 1.0f, boardWet.getNextValue());
            const float wetGain = std::sin (wet * juce::MathConstants<float>::halfPi) * trim;
            const float dryGain = std::cos (wet * juce::MathConstants<float>::halfPi) * trim;

            for (int ch = 0; ch < channels; ++ch)
            {
                auto* wetData = buffer.getWritePointer (ch);
                wetData[i] = wetData[i] * wetGain + dryBuffer.getReadPointer (ch)[readPos] * dryGain;
            }

            if (++readPos >= ringSize)
                readPos = 0;
        }
    }
    else
    {
        outGain.skip (numSamples);
        boardWet.skip (numSamples);
    }

    // 4. the safety limiter, always on. Stereo-linked, so one loud channel ducks both.
    float* left = buffer.getWritePointer (0);
    float* right = channels > 1 ? buffer.getWritePointer (1) : nullptr;

    limiter.process (left, right, numSamples);

    float peak = 0.0f;

    for (int i = 0; i < numSamples; ++i)
    {
        peak = juce::jmax (peak, std::abs (left[i]));

        if (right != nullptr)
            peak = juce::jmax (peak, std::abs (right[i]));
    }

    outPeak.store (peak, std::memory_order_relaxed);
}
} // namespace pb
