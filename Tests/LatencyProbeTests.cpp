#include <juce_core/juce_core.h>
#include <vector>
#include "Engine/LatencyProbe.h"

namespace
{
constexpr int kBlock = 64;

// One device block: the probe writes its click into `out`, so `in` is what a loopback cable would return.
void runBlock (pb::LatencyProbe& probe, const std::vector<float>& in, std::vector<float>& out)
{
    out.assign ((size_t) kBlock, 0.0f);

    const float* inputs[1] = { in.data() };
    float* outputs[1] = { out.data() };

    probe.process (inputs, 1, outputs, 1, kBlock);
}

std::vector<float> silence()
{
    return std::vector<float> ((size_t) kBlock, 0.0f);
}
} // namespace

class LatencyProbeTests final : public juce::UnitTest
{
public:
    LatencyProbeTests() : juce::UnitTest ("LatencyProbe", "engine") {}

    void runTest() override
    {
        beginTest ("peak and first crossing are found where they are");
        {
            const std::vector<float> samples { 0.0f, 0.01f, -0.4f, 0.2f, -0.9f, 0.1f };

            expectWithinAbsoluteError (pb::LatencyProbe::peakOf (samples.data(), (int) samples.size()), 0.9f, 1.0e-6f);
            expectEquals (pb::LatencyProbe::findClick (samples.data(), (int) samples.size(), 0.35f), 2);
            expectEquals (pb::LatencyProbe::findClick (samples.data(), (int) samples.size(), 0.95f), -1);

            const std::vector<float> quiet { 0.0f, 0.001f, 0.0f };
            expectWithinAbsoluteError (pb::LatencyProbe::peakOf (quiet.data(), 3), 0.001f, 1.0e-6f);
            expectEquals (pb::LatencyProbe::findClick (quiet.data(), 3, 0.5f), -1);
        }

        beginTest ("a click that comes back 96 samples later measures as 2 ms at 48k");
        {
            pb::LatencyProbe probe;
            expect (! probe.isPrepared());

            probe.prepare (48000.0, kBlock);
            expect (probe.isPrepared());

            probe.begin();
            expect (probe.isRunning());
            expect (! probe.isFinished());

            // Block 0: the click leaves, nothing returns yet.
            std::vector<float> out;
            runBlock (probe, silence(), out);

            expect (out[0] > 0.5f, "the click should have gone out on the first armed block");
            expectWithinAbsoluteError (pb::LatencyProbe::peakOf (out.data(), kBlock), 0.85f, 1.0e-6f);

            // Block 1: the cable returns it at sample 96 overall, i.e. 32 samples into this block.
            auto returned = silence();
            returned[32] = 0.5f;
            runBlock (probe, returned, out);

            expect (! probe.isFinished());

            // The click must not be written twice.
            expectWithinAbsoluteError (out[0], 0.0f, 1.0e-6f);

            // Fill the half-second window with silence so the probe can finish.
            int blocks = 0;

            while (! probe.isFinished() && blocks < 1000)
            {
                runBlock (probe, silence(), out);
                ++blocks;
            }

            expect (probe.isFinished());
            expect (blocks > 350 && blocks < 400, "the window should be about 375 blocks of 64 at 48k");

            expectWithinAbsoluteError (probe.finish(), 2.0f, 1.0e-3f);
            expectWithinAbsoluteError (probe.resultMs(), 2.0f, 1.0e-3f);
        }

        beginTest ("nothing coming back measures as nothing, not as zero");
        {
            pb::LatencyProbe probe;
            probe.prepare (48000.0, kBlock);
            probe.begin();

            std::vector<float> out;
            int blocks = 0;

            while (! probe.isFinished() && blocks < 1000)
            {
                runBlock (probe, silence(), out);
                ++blocks;
            }

            expect (probe.isFinished());
            expectWithinAbsoluteError (probe.finish(), -1.0f, 1.0e-6f);
            expectWithinAbsoluteError (probe.resultMs(), -1.0f, 1.0e-6f);
        }

        beginTest ("nothing is written or recorded until the probe is armed");
        {
            pb::LatencyProbe probe;
            probe.prepare (48000.0, kBlock);

            std::vector<float> out;
            auto returned = silence();
            returned[5] = 0.5f;
            runBlock (probe, returned, out);

            expectWithinAbsoluteError (pb::LatencyProbe::peakOf (out.data(), kBlock), 0.0f, 1.0e-6f);
            expect (! probe.isRunning());
            expect (! probe.isFinished());

            // An unprepared probe refuses to arm rather than writing somewhere unallocated.
            pb::LatencyProbe fresh;
            fresh.begin();
            expect (! fresh.isRunning());
        }

        beginTest ("a second measurement starts clean");
        {
            pb::LatencyProbe probe;
            probe.prepare (48000.0, kBlock);
            probe.begin();

            // The click leaves on the armed block, so anything the next block returns is one block late:
            // 64 samples, which is 1.33 ms at 48k.
            std::vector<float> out;
            auto returned = silence();
            returned[0] = 0.5f;
            runBlock (probe, silence(), out);
            runBlock (probe, returned, out);

            int blocks = 0;
            while (! probe.isFinished() && blocks < 1000)
            {
                runBlock (probe, silence(), out);
                ++blocks;
            }

            expectWithinAbsoluteError (probe.finish(), 64.0f / 48000.0f * 1000.0f, 1.0e-3f);

            probe.begin();
            expect (probe.isRunning());
            expect (! probe.isFinished());
            expectWithinAbsoluteError (probe.resultMs(), -1.0f, 1.0e-6f);

            // Cancelling abandons the measurement instead of producing a number from it.
            probe.cancel();
            expect (! probe.isRunning());
            expect (! probe.isFinished());
        }
    }
};

static LatencyProbeTests latencyProbeTests;
