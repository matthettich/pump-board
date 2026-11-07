#pragma once
// SCAFFOLD — fixed interface. Do not change without review.
#include <juce_core/juce_core.h>
#include <array>
#include <atomic>

namespace pb
{
// A fixed-size table of every controllable value in the app.
// Registration and lookup by id: message thread. get/set by index: any thread, lock-free.
class ParameterRegistry
{
public:
    static constexpr int kMaxParams = 4096;
    static constexpr int kInvalid = -1;

    struct Entry
    {
        juce::String id;           // "slot/<uid>/<paramId>", "board/bypass", "macro/3"
        juce::String displayName;
        float minValue = 0.0f, maxValue = 1.0f, defaultValue = 0.0f;
        bool isToggle = false;
        bool inUse = false;
    };

    // Message thread. Returns the index, or kInvalid if full. Re-registering an id returns its existing index.
    int add (const Entry& entry);
    void remove (int index);                          // message thread; frees the slot for reuse
    int find (const juce::String& id) const;          // message thread
    const Entry& info (int index) const;              // message thread

    // Any thread. Values are in the param's own range, clamped.
    float get (int index) const noexcept { return values[(size_t) index].load (std::memory_order_relaxed); }
    void set (int index, float value) noexcept;
    void setNormalised (int index, float value01) noexcept;
    float getNormalised (int index) const noexcept;
    void toggle (int index) noexcept;                 // for isToggle entries

    // Bumped on every set(); the UI polls this to know when to repaint.
    uint32_t changeCounter() const noexcept { return counter.load (std::memory_order_relaxed); }

private:
    std::array<Entry, kMaxParams> entries;            // written on message thread only
    std::array<float, kMaxParams> minCache {}, maxCache {}; // read on audio thread; written before the param is used
    std::array<std::atomic<float>, kMaxParams> values {};
    std::atomic<uint32_t> counter { 0 };
};
} // namespace pb
