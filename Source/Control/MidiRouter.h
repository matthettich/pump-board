#pragma once
// SCAFFOLD — fixed interface. Do not change without review.
#include "Control/ParameterRegistry.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <atomic>
#include <memory>
#include <vector>

namespace pb
{
enum class SourceType : uint8_t { Note, CC, ProgramChange, Key };

struct MappingSource
{
    SourceType type = SourceType::Note;
    uint8_t channel = 0;     // 0 = any channel, 1..16
    int number = 0;          // note, CC number, program, or juce::KeyPress code for Key
};

enum class ActionType : uint8_t
{
    Toggle,         // note-on or key press flips an isToggle param
    Momentary,      // on while held (note-on / note-off, key down / up)
    CCThreshold,    // CC >= 64 = on, < 64 = off
    SetValue,       // CC 0..127 maps linearly to [minValue, maxValue] of the target
    PresetSelect,   // program change n -> preset n (posted to the message thread)
    PresetNext,
    PresetPrev
};

struct Mapping
{
    MappingSource source;
    ActionType action = ActionType::Toggle;
    int paramIndex = ParameterRegistry::kInvalid; // unused for preset actions
    float minValue = 0.0f, maxValue = 1.0f;       // SetValue range, in the target's units
};

// Immutable table, swapped like ChainSnapshot.
struct MappingTable { std::vector<Mapping> mappings; };

// Events the audio thread hands back to the message thread.
struct ControlEvent
{
    enum class Kind : uint8_t { PresetSelect, PresetNext, PresetPrev, LearnCaptured } kind;
    MappingSource source;   // for LearnCaptured
    int value = 0;          // preset number
};

class MidiRouter
{
public:
    explicit MidiRouter (ParameterRegistry& registry);
    ~MidiRouter();

    // ---- Message thread ----
    void publish (std::unique_ptr<MappingTable> next);   // retire + free like BoardProcessor
    void collectGarbage();
    void beginLearn();   // next incoming note/CC/PC is captured, not acted on
    void cancelLearn();
    // Drain events from the audio thread. Call from the ~30 Hz timer.
    template <typename Fn> void drainEvents (Fn&& handler);

    // Keys come from the UI on the message thread; they use the same table.
    // Preset actions from keys are returned here directly (never pushed into the FIFO,
    // which has exactly one producer: the audio thread).
    // Returns true and fills `presetEvent` when the key maps to a preset action.
    bool handleKey (int keyCode, bool isDown, ControlEvent& presetEvent);

    // ---- AUDIO THREAD ----
    void handle (const juce::MidiBuffer& midi) noexcept;

private:
    void apply (const MappingSource& src, bool isOn, int value) noexcept;
    void pushEvent (const ControlEvent& e) noexcept;

    ParameterRegistry& registry;
    std::atomic<MappingTable*> active { nullptr };
    std::atomic<bool> learning { false };

    static constexpr int kFifoSize = 256;
    juce::AbstractFifo fifo { kFifoSize };
    std::array<ControlEvent, kFifoSize> events {};
};

template <typename Fn>
void MidiRouter::drainEvents (Fn&& handler)
{
    const auto scope = fifo.read (fifo.getNumReady());
    for (int i = 0; i < scope.blockSize1; ++i) handler (events[(size_t) (scope.startIndex1 + i)]);
    for (int i = 0; i < scope.blockSize2; ++i) handler (events[(size_t) (scope.startIndex2 + i)]);
}
} // namespace pb
