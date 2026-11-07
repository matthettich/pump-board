#include "Control/KeyRouter.h"

namespace pb
{
KeyRouter::KeyRouter (MidiRouter& routerIn)
    : router (routerIn)
{
}

bool KeyRouter::keyDown (int keyCode)
{
    if (keyCode == 0)                    // a bare modifier change carries no key of its own
        return false;

    if (isHeld (keyCode))                // auto-repeat: nothing changed, and a Toggle must not flip twice
        return true;

    addHeld (keyCode);

    if (learning)
    {
        // The next key is the mapping's source, and it must not also act on the table it is replacing.
        learning = false;

        if (onLearnCaptured != nullptr)
            onLearnCaptured (keyCode);

        return true;
    }

    ControlEvent event;

    if (router.handleKey (keyCode, true, event))
    {
        if (onPresetAction != nullptr)
            onPresetAction (event);

        return true;
    }

    return false;
}

bool KeyRouter::keyUp (int keyCode)
{
    if (! isHeld (keyCode))
        return false;                    // a release we never saw the press for: nothing to undo

    removeHeld (keyCode);

    ControlEvent event;

    if (router.handleKey (keyCode, false, event))
    {
        if (onPresetAction != nullptr)
            onPresetAction (event);

        return true;
    }

    return false;
}

void KeyRouter::beginLearn()
{
    learning = true;
}

void KeyRouter::cancelLearn()
{
    learning = false;
}

bool KeyRouter::isHeld (int keyCode) const noexcept
{
    for (int i = 0; i < numHeld; ++i)
        if (held[(size_t) i] == keyCode)
            return true;

    return false;
}

void KeyRouter::addHeld (int keyCode) noexcept
{
    if (numHeld >= kMaxHeldKeys)
        return;                          // more than sixteen keys at once: the extra ones are not held

    held[(size_t) numHeld++] = keyCode;
}

void KeyRouter::removeHeld (int keyCode) noexcept
{
    for (int i = 0; i < numHeld; ++i)
    {
        if (held[(size_t) i] != keyCode)
            continue;

        held[(size_t) i] = held[(size_t) (--numHeld)];
        return;
    }
}
} // namespace pb
