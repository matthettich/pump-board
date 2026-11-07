#include "Effects/adapters/PumpVoxAdapter.h"
#include "Effects/adapters/PumpChoices.h"
#include <cmath>
#include <cstring>

namespace pb
{
namespace
{
enum class Kind { none, engine, mode, bands, fft, source, wave, chord, key, scale, filter, stack };

struct Row
{
    const char* id;
    const char* name;
    float lo, hi, def;
    const char* unit;
    bool toggle;
    float skew;
    bool integer;
    Kind kind;
};

// pump's VoxParams.hpp, symbol for symbol (the fx_ rows after the vocoder's), minus route / bypass / fx_note (which
// is the shared `note`), with the adapter's own `source` and `note`. Log and power scaled knobs carry the value pump's
// knob shows at its middle.
const std::array<Row, PumpVoxAdapter::kNumParams>& table()
{
    using P = PumpVoxAdapter;
    static const std::array<Row, P::kNumParams> rows = []
    {
        std::array<Row, P::kNumParams> t {};
        auto set = [&t] (int i, Row r) { t[(size_t) i] = r; };

        set (P::kEngine,    { "engine",      "Engine",          0.0f,    1.0f,     0.0f, "",   false, 0.0f, true,  Kind::engine });
        set (P::kMode,      { "mode",        "Mode",            0.0f,    3.0f,     0.0f, "",   false, 0.0f, true,  Kind::mode });
        set (P::kBands,     { "bands",       "Bands",           0.0f,    6.0f,     2.0f, "",   false, 0.0f, true,  Kind::bands });
        set (P::kLow,       { "low",         "Lowest Band",    40.0f, 1000.0f,   100.0f, "Hz", false, std::sqrt (40.0f * 1000.0f), false, Kind::none });
        set (P::kHigh,      { "high",        "Highest Band", 1000.0f, 16000.0f, 8000.0f, "Hz", false, std::sqrt (1000.0f * 16000.0f), false, Kind::none });
        set (P::kWidth,     { "width",       "Band Width",      0.5f,    2.0f,     1.0f, "",   false, 1.0f, false, Kind::none });
        set (P::kDetail,    { "detail",      "Detail",          0.0f,  100.0f,    50.0f, "%",  false, 0.0f, false, Kind::none });
        set (P::kFftSize,   { "fft_size",    "FFT Size",        0.0f,    3.0f,     1.0f, "",   false, 0.0f, true,  Kind::fft });
        set (P::kTube,      { "tube",        "Tube",            0.0f,  100.0f,    35.0f, "%",  false, 0.0f, false, Kind::none });
        set (P::kDrive,     { "drive",       "Driver Drive",    0.0f,   24.0f,     6.0f, "dB", false, 0.0f, false, Kind::none });
        set (P::kSmear,     { "smear",       "Smear",           0.0f,  100.0f,    50.0f, "%",  false, 0.0f, false, Kind::none });
        set (P::kFormant,   { "formant",     "Formant",       -12.0f,   12.0f,     0.0f, "st", false, 0.0f, false, Kind::none });
        set (P::kAttack,    { "attack",      "Attack",          0.5f,  100.0f,     4.0f, "ms", false, std::sqrt (0.5f * 100.0f), false, Kind::none });
        set (P::kRelease,   { "release",     "Release",         5.0f, 1000.0f,    60.0f, "ms", false, std::sqrt (5.0f * 1000.0f), false, Kind::none });
        set (P::kFlatten,   { "flatten",     "Carrier Flatten", 0.0f,  100.0f,    40.0f, "%",  false, 0.0f, false, Kind::none });
        set (P::kEmphasis,  { "emphasis",    "Emphasis",        0.0f,  100.0f,    30.0f, "%",  false, 0.0f, false, Kind::none });
        set (P::kSibilance, { "sibilance",   "Sibilance",       0.0f,  100.0f,    30.0f, "%",  false, 0.0f, false, Kind::none });
        set (P::kNoise,     { "noise",       "Unvoiced Noise",  0.0f,  100.0f,    30.0f, "%",  false, 0.0f, false, Kind::none });
        set (P::kGate,      { "gate",        "Voice Gate",    -90.0f,  -20.0f,   -90.0f, "dB", false, 0.0f, false, Kind::none });    // -90 is "off"
        set (P::kFreeze,    { "freeze",      "Freeze",          0.0f,    1.0f,     0.0f, "",   true,  0.0f, true,  Kind::none });
        set (P::kSource,    { "source",      "Carrier Source",  0.0f,    1.0f,     0.0f, "",   false, 0.0f, true,  Kind::source });
        set (P::kNote,      { "note",        "Note",           24.0f,   84.0f,    45.0f, "",   false, 0.0f, true,  Kind::none });
        set (P::kWave,      { "wave",        "Synth Wave",      0.0f,    4.0f,     0.0f, "",   false, 0.0f, true,  Kind::wave });
        set (P::kOctave,    { "octave",      "Synth Octave",   -2.0f,    2.0f,     0.0f, "",   false, 0.0f, true,  Kind::none });
        set (P::kChord,     { "chord",       "Follow Chord",    0.0f,    6.0f,     0.0f, "",   false, 0.0f, true,  Kind::chord });
        set (P::kKey,       { "key",         "Key",             0.0f,   11.0f,     0.0f, "",   false, 0.0f, true,  Kind::key });
        set (P::kScale,     { "scale",       "Scale",           0.0f,   14.0f,     1.0f, "",   false, 0.0f, true,  Kind::scale });
        set (P::kGlide,     { "glide",       "Follow Glide",    0.0f,  500.0f,    40.0f, "ms", false, 125.0f, false, Kind::none });   // pump: power 2
        set (P::kDetune,    { "detune",      "Synth Detune",    0.0f,  100.0f,    25.0f, "%",  false, 0.0f, false, Kind::none });
        set (P::kSpread,    { "spread",      "Stereo Spread",   0.0f,  100.0f,    30.0f, "%",  false, 0.0f, false, Kind::none });
        set (P::kEnsemble,  { "ensemble",    "Ensemble",        0.0f,  100.0f,     0.0f, "%",  false, 0.0f, false, Kind::none });
        set (P::kVoiceDry,  { "voice_dry",   "Dry Voice",       0.0f,  100.0f,     0.0f, "%",  false, 0.0f, false, Kind::none });
        set (P::kCarrierDry,{ "carrier_dry", "Dry Carrier",     0.0f,  100.0f,     0.0f, "%",  false, 0.0f, false, Kind::none });
        set (P::kOutGain,   { "out_gain",    "Output Gain",   -24.0f,   24.0f,     0.0f, "dB", false, 0.0f, false, Kind::none });

        set (P::kFxPitchOn, { "fx_pitch_on", "FX Pitch On",     0.0f,    1.0f,     1.0f, "",   true,  0.0f, true,  Kind::none });
        set (P::kFxPitch,   { "fx_pitch",    "FX Pitch",      -24.0f,   24.0f,     0.0f, "st", false, 0.0f, false, Kind::none });
        set (P::kFxFormant, { "fx_formant",  "FX Formant",    -12.0f,   12.0f,     0.0f, "st", false, 0.0f, false, Kind::none });
        set (P::kFxSub,     { "fx_sub",      "FX Sub Octave",   0.0f,  100.0f,     0.0f, "%",  false, 0.0f, false, Kind::none });
        set (P::kFxGritOn,  { "fx_grit_on",  "FX Grit On",      0.0f,    1.0f,     0.0f, "",   true,  0.0f, true,  Kind::none });
        set (P::kFxFry,     { "fx_fry",      "FX Fry",          0.0f,  100.0f,    40.0f, "%",  false, 0.0f, false, Kind::none });
        set (P::kFxRasp,    { "fx_rasp",     "FX Rasp",         0.0f,  100.0f,    30.0f, "%",  false, 0.0f, false, Kind::none });
        set (P::kFxRattle,  { "fx_rattle",   "FX Rattle",       0.0f,  100.0f,    20.0f, "%",  false, 0.0f, false, Kind::none });
        set (P::kFxRattleHz,{ "fx_rattle_hz","FX Rattle Rate", 10.0f,   90.0f,    38.0f, "Hz", false, 0.0f, false, Kind::none });
        set (P::kFxDrive,   { "fx_drive",    "FX Drive",        0.0f,   36.0f,     9.0f, "dB", false, 0.0f, false, Kind::none });
        set (P::kFxTone,    { "fx_tone",     "FX Tone",      -100.0f,  100.0f,     0.0f, "%",  false, 0.0f, false, Kind::none });
        set (P::kFxRobotOn, { "fx_robot_on", "FX Robot On",     0.0f,    1.0f,     0.0f, "",   true,  0.0f, true,  Kind::none });
        set (P::kFxMono,    { "fx_mono",     "FX Monotone",     0.0f,  100.0f,   100.0f, "%",  false, 0.0f, false, Kind::none });
        set (P::kFxGlide,   { "fx_glide",    "FX Robot Glide",  0.0f,  500.0f,    30.0f, "ms", false, 125.0f, false, Kind::none });
        set (P::kFxRing,    { "fx_ring",     "FX Ring",         0.0f,  100.0f,     0.0f, "%",  false, 0.0f, false, Kind::none });
        set (P::kFxRingHz,  { "fx_ring_hz",  "FX Ring Frequency", 10.0f, 2000.0f, 110.0f, "Hz", false, std::sqrt (10.0f * 2000.0f), false, Kind::none });
        set (P::kFxComb,    { "fx_comb",     "FX Comb",         0.0f,  100.0f,     0.0f, "%",  false, 0.0f, false, Kind::none });
        set (P::kFxCrush,   { "fx_crush",    "FX Crush",        0.0f,  100.0f,     0.0f, "%",  false, 0.0f, false, Kind::none });
        set (P::kFxCharOn,  { "fx_char_on",  "FX Character On", 0.0f,    1.0f,     0.0f, "",   true,  0.0f, true,  Kind::none });
        set (P::kFxFilter,  { "fx_filter",   "FX Filter",       0.0f,    4.0f,     0.0f, "",   false, 0.0f, true,  Kind::filter });
        set (P::kFxStack,   { "fx_stack",    "FX Stack",        0.0f,    3.0f,     0.0f, "",   false, 0.0f, true,  Kind::stack });
        set (P::kFxStackMix,{ "fx_stack_mix","FX Stack Mix",    0.0f,  100.0f,    50.0f, "%",  false, 0.0f, false, Kind::none });
        set (P::kFxDouble,  { "fx_double",   "FX Double",       0.0f,  100.0f,     0.0f, "%",  false, 0.0f, false, Kind::none });
        set (P::kFxSlap,    { "fx_slap",     "FX Slap",         0.0f,  100.0f,     0.0f, "%",  false, 0.0f, false, Kind::none });
        set (P::kFxSlapMs,  { "fx_slap_ms",  "FX Slap Time",   20.0f,  280.0f,   110.0f, "ms", false, 0.0f, false, Kind::none });
        set (P::kFxMix,     { "fx_mix",      "FX Mix",          0.0f,  100.0f,   100.0f, "%",  false, 0.0f, false, Kind::none });
        return t;
    }();

    return rows;
}

std::vector<juce::String> choicesFor (Kind kind)
{
    switch (kind)
    {
        case Kind::engine: return pb::choices::names (pump::vox::kEngineNames, pump::vox::kNumEngines);
        case Kind::mode:   return pb::choices::names (pump::vox::kModeNames, pump::vox::kNumModes);
        case Kind::bands:  return pb::choices::names (pump::vox::kBandNames, pump::vox::kNumBandChoices);
        case Kind::fft:    return pb::choices::names (pump::vox::kFftNames, pump::vox::kNumFft);
        case Kind::source: return { "Follow voice", "Held note" };
        case Kind::wave:   return pb::choices::names (pump::vox::kWaveNames, pump::vox::kNumWaves);
        case Kind::chord:  return pb::choices::names (pump::vox::kChordNames, pump::vox::kNumChords);
        case Kind::key:    return pb::choices::names (pump::vox::kKeyNamesV, 12);
        case Kind::scale:  return pb::choices::names (pump::vox::kScaleNamesV, pump::vox::kNumScaleChoices);
        case Kind::filter: return pb::choices::names (pump::vox::kFxFilterNamesP, 5);
        case Kind::stack:  return pb::choices::names (pump::vox::kFxStackNamesP, 4);
        case Kind::none:   break;
    }

    return {};
}
} // namespace

PumpVoxAdapter::PumpVoxAdapter()
    : engine (std::make_unique<pump::vox::Engine>()), fx (std::make_unique<pump::vox::VoiceFx>())
{
    const auto& rows = table();

    for (int i = 0; i < kNumParams; ++i)
        values[(size_t) i] = rows[(size_t) i].def;

    appliedEngine = ivalue (kEngine);
    appliedMode = ivalue (kMode);
    appliedFft = ivalue (kFftSize);
}

std::vector<ParamInfo> PumpVoxAdapter::params() const
{
    std::vector<ParamInfo> out;
    out.reserve ((size_t) kNumParams);

    for (const auto& r : table())
        out.emplace_back (r.id, r.name, r.lo, r.hi, r.def, r.unit, r.toggle, r.skew, choicesFor (r.kind));

    return out;
}

pump::vox::Settings PumpVoxAdapter::makeVocoderSettings() const
{
    pump::vox::Settings s;
    s.mode = appliedMode;
    s.route = pump::vox::kRouteSynth;                       // fixed: no sidechain in a slot
    s.bandsIndex = ivalue (kBands);
    s.lowHz = (double) fvalue (kLow);
    s.highHz = (double) fvalue (kHigh);
    s.width = (double) fvalue (kWidth);
    s.detail = (double) fvalue (kDetail);
    s.fftIndex = appliedFft;
    s.tube = (double) fvalue (kTube);
    s.driveDb = (double) fvalue (kDrive);
    s.smear = (double) fvalue (kSmear);
    s.formant = (double) fvalue (kFormant);
    s.attackMs = (double) fvalue (kAttack);
    s.releaseMs = (double) fvalue (kRelease);
    s.flatten = (double) fvalue (kFlatten);
    s.emphasis = (double) fvalue (kEmphasis);
    s.sibilance = (double) fvalue (kSibilance);
    s.noise = (double) fvalue (kNoise);
    s.gateDb = (double) fvalue (kGate);
    s.freeze = bvalue (kFreeze);
    s.source = ivalue (kSource) == kSourceHeld ? pump::vox::kSourceMidi : pump::vox::kSourceFollow;
    s.wave = ivalue (kWave);
    s.octave = ivalue (kOctave);
    s.chord = ivalue (kChord);
    s.key = ivalue (kKey);
    s.scale = ivalue (kScale);
    s.glideMs = (double) fvalue (kGlide);
    s.detune = (double) fvalue (kDetune);
    s.spread = (double) fvalue (kSpread);
    s.ensemble = (double) fvalue (kEnsemble);
    s.voiceDry = (double) fvalue (kVoiceDry);
    s.carrierDry = (double) fvalue (kCarrierDry);
    s.outDb = (double) fvalue (kOutGain);

    return s;
}

pump::vox::FxSettings PumpVoxAdapter::makeFxSettings() const
{
    pump::vox::FxSettings f;
    f.pitchOn = bvalue (kFxPitchOn);
    f.pitch = fvalue (kFxPitch);
    f.formant = fvalue (kFxFormant);
    f.sub = fvalue (kFxSub);
    f.gritOn = bvalue (kFxGritOn);
    f.fry = fvalue (kFxFry);
    f.rasp = fvalue (kFxRasp);
    f.rattle = fvalue (kFxRattle);
    f.rattleHz = fvalue (kFxRattleHz);
    f.drive = fvalue (kFxDrive);
    f.tone = fvalue (kFxTone);
    f.robotOn = bvalue (kFxRobotOn);
    f.mono = fvalue (kFxMono);
    f.note = ivalue (kNote);                                // the robot's note without MIDI
    f.glideMs = fvalue (kFxGlide);
    f.ring = fvalue (kFxRing);
    f.ringHz = fvalue (kFxRingHz);
    f.comb = fvalue (kFxComb);
    f.crush = fvalue (kFxCrush);
    f.charOn = bvalue (kFxCharOn);
    f.filter = ivalue (kFxFilter);
    f.stack = ivalue (kFxStack);
    f.stackMix = fvalue (kFxStackMix);
    f.doubler = fvalue (kFxDouble);
    f.slap = fvalue (kFxSlap);
    f.slapMs = fvalue (kFxSlapMs);
    f.mix = fvalue (kFxMix);
    f.outDb = fvalue (kOutGain);

    return f;
}

void PumpVoxAdapter::pushSettings() noexcept
{
    engine->setSettings (makeVocoderSettings());
    fx->set (makeFxSettings());
    ++pushes;
    dirty = false;
}

bool PumpVoxAdapter::structurePending() const noexcept
{
    return ivalue (kEngine) != appliedEngine || ivalue (kMode) != appliedMode || ivalue (kFftSize) != appliedFft;
}

void PumpVoxAdapter::applyStructure() noexcept
{
    const bool engineChanged = ivalue (kEngine) != appliedEngine;

    appliedEngine = ivalue (kEngine);
    appliedMode = ivalue (kMode);
    appliedFft = ivalue (kFftSize);

    if (engineChanged)
    {
        // pump's plugin does the same once its 10 ms fade-out has finished: both engines start clean.
        fx->reset();
        engine->reset();
        sentNote = -1;                  // the vocoder's synth forgot its notes
    }

    dirty = true;                       // mode / fft size reach pump's engine through its normal settings path
}

void PumpVoxAdapter::prepare (double sampleRate, int maxBlockSize, int numChannels)
{
    juce::ignoreUnused (numChannels);

    preparedRate = sampleRate;
    preparedBlock = juce::jmax (1, maxBlockSize);
    rightScratch.assign ((size_t) preparedBlock, 0.0f);
    voiceScratch.assign ((size_t) preparedBlock, 0.0f);

    // prepare takes the stored settings as they are, structure included (so the latency is right from block one).
    appliedEngine = ivalue (kEngine);
    appliedMode = ivalue (kMode);
    appliedFft = ivalue (kFftSize);

    // Same order as pump's PluginVox::prepareAll(): settings first, then prepare. The vocoder's engine adopts its
    // pending settings in prepare; the voice FX engine does too.
    engine->setSettings (makeVocoderSettings());
    engine->prepare (preparedRate, preparedBlock);
    fx->set (makeFxSettings());
    fx->prepare (preparedRate);

    ++pushes;
    dirty = false;
    sentNote = -1;
    silentBlocks = 0;
    prepared = true;
}

void PumpVoxAdapter::reset()
{
    if (! prepared)
        return;

    prepare (preparedRate, preparedBlock, 2);       // a full re-initialise: after reset == after pump's own prepare
}

void PumpVoxAdapter::setParam (int index, float value) noexcept
{
    if (index < 0 || index >= kNumParams || ! std::isfinite (value))
        return;

    const Row& row = table()[(size_t) index];
    float v = juce::jlimit (row.lo, row.hi, value);

    if (row.integer)
        v = (float) std::lround (v);

    float& slot = values[(size_t) index];

    if (std::abs (slot - v) <= 1.0e-6f)
        return;                         // a steady knob: nothing to tell the engine

    slot = v;

    if (! isStructural (index))
    {
        dirty = true;
        return;
    }

    // Structural: waits for silence once the pedal is running (see the header); before that it simply takes effect.
    if (! prepared)
    {
        appliedEngine = ivalue (kEngine);
        appliedMode = ivalue (kMode);
        appliedFft = ivalue (kFftSize);
        dirty = true;
    }
}

void PumpVoxAdapter::process (juce::AudioBuffer<float>& buffer) noexcept
{
    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    if (numSamples <= 0 || numChannels <= 0 || ! prepared)
        return;

    if ((size_t) numSamples > rightScratch.size())
        return;                          // never take a block bigger than prepare() promised

    auto* left = buffer.getWritePointer (0);
    float* right = nullptr;

    if (numChannels > 1)
    {
        right = buffer.getWritePointer (1);
    }
    else
    {
        std::memcpy (rightScratch.data(), left, (size_t) numSamples * sizeof (float));
        right = rightScratch.data();
    }

    // Quiet blocks open the door for a structural change (engine / mode / FFT size: they move the latency).
    float peak = 0.0f;

    for (int i = 0; i < numSamples; ++i)
        peak = juce::jmax (peak, std::abs (left[i]), std::abs (right[i]));

    silentBlocks = peak < 1.0e-4f ? silentBlocks + 1 : 0;

    if (silentBlocks >= 2 && structurePending())
        applyStructure();

    if (dirty)
        pushSettings();                  // block start, as pump's engines expect

    if (appliedEngine == pump::vox::kEngineVoiceFx)
    {
        // The voice is the input's mono sum, as in pump's plugin; the engine returns a stereo voice.
        float* voice = voiceScratch.data();

        for (int i = 0; i < numSamples; ++i)
            voice[i] = 0.5f * (left[i] + right[i]);

        fx->process (voice, left, right, numSamples);
    }
    else
    {
        // "Held note": play the one note ourselves, as the MIDI a held key would send. The engine ignores note-ons
        // while its synth is still on Follow, so wait until it has adopted the MIDI source (one block after the knob).
        const bool wantHeld = ivalue (kSource) == kSourceHeld && engine->activeSettings().source == pump::vox::kSourceMidi;
        const int wantNote = wantHeld ? ivalue (kNote) : -1;
        int numEvents = 0;

        if (wantNote != sentNote)
        {
            if (sentNote >= 0)
                events[(size_t) numEvents++] = { 0u, 0x80, (uint8_t) sentNote, 0 };

            if (wantNote >= 0)
                events[(size_t) numEvents++] = { 0u, 0x90, (uint8_t) wantNote, 100 };

            sentNote = wantNote;
        }

        // Route "Synth carrier": the input is the voice, the carrier is the built-in synth, no sidechain.
        engine->process (left, right, nullptr, nullptr, left, right, numSamples, events.data(), numEvents);
    }

    if (numChannels == 1)
        for (int i = 0; i < numSamples; ++i)
            left[i] = 0.5f * (left[i] + rightScratch[(size_t) i]);
}
} // namespace pb
