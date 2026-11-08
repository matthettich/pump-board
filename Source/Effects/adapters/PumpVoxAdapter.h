#pragma once
// pump VOX behind IEffect: a vocoder with a built-in synth carrier, plus VOX's creative VOICE FX engine (pitch and
// formant, grit, robot, character). pump's engines (`pump::vox::Engine` and `pump::vox::VoiceFx`) are used as-is;
// this file only translates. Ids, ranges, defaults and units match pump's VoxParams.hpp, and the two mappings to
// `pump::vox::Settings` / `FxSettings` are pump's own `PluginVox::settings()` / `fxSettings()`.
//
// WHAT A SLOT CAN DO — and what is deliberately left out:
//   - ROUTING is fixed to "Synth carrier": the input is the voice and the carrier is VOX's built-in synth. The other
//     three routes need a second signal (Sidechain carrier, Sidechain voice) or a left/right split (Split L/R: left =
//     carrier, right = voice). A slot has one stereo input, no sidechain bus, and the board's signal is a mono voice
//     or guitar on both sides, so there is no second signal to route. The engine is therefore driven with no sidechain.
//   - The synth's SOURCE offers FOLLOW (the carrier tracks the voice's pitch, snapped to KEY / SCALE, with a CHORD) and
//     HELD NOTE. Pump's MIDI source needs MIDI notes; a pedal has no MIDI input into the audio path, so "Held note"
//     makes the adapter play ONE note itself: it hands the engine a note-on for NOTE (and a note-off when NOTE or the
//     source changes), exactly the events pump's plugin would receive from a held key. pump's AUTO (MIDI while a key is
//     held, else follow) is the same as FOLLOW here, since no key is ever held, so it is not offered.
//   - In the VOICE FX engine the robot's pitch is NOTE (pump's "NOTE without MIDI"); the same NOTE knob serves both
//     engines. The FX engine's route (sidechain / split voice) is likewise fixed to the input.
//   - pump's own `bypass` is not duplicated: the slot's on/off switch is the board's bypass. FREEZE is kept.
//
// STRUCTURAL CHANGES WAIT FOR SILENCE (as TUBE's oversampling does, card 5.33): ENGINE, MODE and FFT SIZE move the
// pedal's latency (Spectral/Convolve add the FFT size; the Voice FX engine adds its ~24 ms PSOLA; Classic/Talkbox add
// nothing) and nothing compensates latency across slots, so a change under a held note would shift everything after
// this pedal. They are stored at once and take effect once the input has been quiet for two blocks, or straight away
// when nothing is playing yet (prepare/reset). pump's own plugin fades 10 ms across such a switch; here the switch
// happens in silence, so the engine's own fade (mode) is all that remains.
#include "Effects/IEffect.h"
#include "VoxEngine.hpp"
#include "VoxFx.hpp"
#include <array>
#include <cmath>
#include <memory>
#include <vector>

namespace pb
{
class PumpVoxAdapter final : public IEffect
{
public:
    enum ParamIndex
    {
        kEngine = 0, kMode, kBands, kLow, kHigh, kWidth, kDetail, kFftSize, kTube, kDrive, kSmear, kFormant,
        kAttack, kRelease, kFlatten, kEmphasis, kSibilance, kNoise, kGate, kFreeze,
        kSource, kNote, kWave, kOctave, kChord, kKey, kScale, kGlide, kDetune, kSpread, kEnsemble,
        kVoiceDry, kCarrierDry, kOutGain,
        kFxPitchOn, kFxPitch, kFxFormant, kFxSub,
        kFxGritOn, kFxFry, kFxRasp, kFxRattle, kFxRattleHz, kFxDrive, kFxTone,
        kFxRobotOn, kFxMono, kFxGlide, kFxRing, kFxRingHz, kFxComb, kFxCrush,
        kFxCharOn, kFxFilter, kFxStack, kFxStackMix, kFxDouble, kFxSlap, kFxSlapMs, kFxMix,
        kNumParams
    };

    // The adapter's own SOURCE switch (pump's has three: Auto / MIDI / Follow).
    enum Source { kSourceFollow = 0, kSourceHeld };

    PumpVoxAdapter();

    juce::String typeId() const override { return "pump.vox"; }
    juce::String displayName() const override { return "pump VOX"; }
    std::vector<ParamInfo> params() const override;

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void reset() override;
    void setParam (int index, float value) noexcept override;
    void process (juce::AudioBuffer<float>& buffer) noexcept override;

    // The active engine's own figure: 0 for Classic / Talkbox, the FFT size for Spectral / Convolve, and the PSOLA's
    // ~24 ms for the Voice FX engine. It follows an applied structural change once pump's engine has switched
    // (the vocoder fades its mode over 10 ms first), so it can lag by a few blocks.
    int latencySamples() const noexcept override
    {
        if (! prepared)
            return 0;

        return appliedEngine == pump::vox::kEngineVoiceFx ? fx->latency() : engine->latency();
    }

    double tailSeconds() const noexcept override { return 1.0; }     // release up to 1 s, ensemble, slap

    // What the adapter hands pump's engines; the null tests drive the engines directly with the same structs.
    pump::vox::Settings makeVocoderSettings() const;
    pump::vox::FxSettings makeFxSettings() const;
    int activeEngineKind() const noexcept { return appliedEngine; }
    int heldNoteSent() const noexcept { return sentNote; }           // -1: none
    int settingsPushCount() const noexcept { return pushes; }
    float paramValue (int index) const noexcept { return values[(size_t) index]; }

private:
    static bool isStructural (int index) noexcept { return index == kEngine || index == kMode || index == kFftSize; }
    bool structurePending() const noexcept;
    void applyStructure() noexcept;
    void pushSettings() noexcept;

    int ivalue (int index) const noexcept { return (int) std::lround (values[(size_t) index]); }
    float fvalue (int index) const noexcept { return values[(size_t) index]; }
    bool bvalue (int index) const noexcept { return values[(size_t) index] >= 0.5f; }

    std::unique_ptr<pump::vox::Engine> engine;        // on the heap: the registry builds throwaway instances
    std::unique_ptr<pump::vox::VoiceFx> fx;

    std::array<float, kNumParams> values {};          // what the user asked for, clamped; defaults filled in the ctor
    int appliedEngine = pump::vox::kEngineVocoder;    // what is actually running (structural params wait for silence)
    int appliedMode = pump::vox::kClassic;
    int appliedFft = 1;

    bool dirty = true;
    bool prepared = false;
    int pushes = 0;
    int silentBlocks = 0;
    int sentNote = -1;                                 // the note the engine has been told to hold, -1 for none
    std::array<pump::vox::MidiMsg, 4> events {};
    double preparedRate = 48000.0;
    int preparedBlock = 512;
    std::vector<float> rightScratch, voiceScratch;
};
} // namespace pb
