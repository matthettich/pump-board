// Card 1.1 evidence: pump's engines compile and run inside our build, with no plugin wrapper.
// Card 1.3 replaces this probe with the real adapters and the null tests.

#include <juce_core/juce_core.h>
#include <vector>
#include <cmath>

#include "SpringEngine.hpp"
#include "EchoEngine.hpp"
#include "TubeEngine.hpp"

namespace
{
constexpr double kSampleRate = 48000.0;
constexpr int kBlock = 256;

void fillTestSignal (std::vector<float>& l, std::vector<float>& r, int startSample)
{
    for (int i = 0; i < (int) l.size(); ++i)
    {
        const double t = (double) (startSample + i) / kSampleRate;
        const auto s = (float) (0.2 * std::sin (2.0 * 3.14159265358979323846 * 220.0 * t));
        l[(size_t) i] = s;
        r[(size_t) i] = s * 0.8f;
    }
}

bool isFinite (const std::vector<float>& v)
{
    for (auto s : v)
        if (! std::isfinite (s))
            return false;

    return true;
}

float peakOf (const std::vector<float>& v)
{
    float peak = 0.0f;
    for (auto s : v)
        peak = juce::jmax (peak, std::abs (s));

    return peak;
}

// Runs n blocks of a sine through an engine and returns whether it stayed finite and audible.
template <typename Engine, typename Settings>
bool runsStandalone (Engine& engine, const Settings& settings, int numBlocks, float& peakOut)
{
    engine.prepare (kSampleRate, kBlock);
    engine.setSettings (settings);

    std::vector<float> l ((size_t) kBlock), r ((size_t) kBlock);
    peakOut = 0.0f;

    for (int b = 0; b < numBlocks; ++b)
    {
        fillTestSignal (l, r, b * kBlock);
        engine.process (l.data(), r.data(), kBlock);

        if (! isFinite (l) || ! isFinite (r))
            return false;

        peakOut = juce::jmax (peakOut, peakOf (l), peakOf (r));
    }

    return peakOut > 0.0f;
}
} // namespace

class PumpDspProbeTests final : public juce::UnitTest
{
public:
    PumpDspProbeTests() : juce::UnitTest ("PumpDspProbe", "dsp") {}

    void runTest() override
    {
        beginTest ("pump SPRING engine runs standalone");
        {
            pump::spring::Engine engine;
            pump::spring::Settings settings;
            settings.mix = 30.0;
            settings.decay = 2.0;

            float peak = 0.0f;
            expect (runsStandalone (engine, settings, 16, peak));
            expect (peak > 0.0f);
            expect (engine.latency() == 0);
        }

        beginTest ("pump ECHO engine runs standalone");
        {
            pump::echo::Engine engine;
            pump::echo::Settings settings;
            settings.mix = 30.0;
            engine.setTempo (120.0);

            float peak = 0.0f;
            expect (runsStandalone (engine, settings, 16, peak));
            expect (peak > 0.0f);
            expect (engine.latency() == 0);
        }

        beginTest ("pump TUBE engine runs standalone");
        {
            pump::tube::Engine engine;
            pump::tube::Settings settings;
            settings.mix = 100.0;
            settings.oversampling = 2;

            float peak = 0.0f;
            expect (runsStandalone (engine, settings, 16, peak));
            expect (peak > 0.0f);
            expect (engine.latency() >= 0);   // the oversampler adds latency; the chain must allow for it
        }
    }
};

static PumpDspProbeTests pumpDspProbeTests;
