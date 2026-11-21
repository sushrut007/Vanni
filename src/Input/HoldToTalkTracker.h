#pragma once

#include "Input/VaaniHotkey.h"

#include <QtGlobal>

class HoldToTalkTracker
{
public:
    static constexpr qint64 kSettleMs = 250;
    static constexpr qint64 kDoubleTapWindowMs = 500;
    static constexpr qint64 kToggleMaxMs = 10 * 60 * 1000;

    enum class Event { None, Started, Ended, Cancelled, Toggled };
    struct Step {
        Event event = Event::None;
    };

    void setHotkey(const VaaniHotkey& hotkey) noexcept;
    const VaaniHotkey& hotkey() const noexcept { return m_hotkey; }

    void setDoubleTapToggleEnabled(bool enabled) noexcept;
    bool doubleTapToggleEnabled() const noexcept { return m_doubleTapEnabled; }

    Step feed(bool fullyHeld, bool partiallyHeld, bool otherKeyDown, qint64 nowMs,
              bool escapeDown = false) noexcept;

    void reset() noexcept;

    bool sessionActive() const noexcept { return m_state == State::Active || m_state == State::Toggle; }
    bool toggleActive() const noexcept { return m_state == State::Toggle; }

private:
    enum class State { Idle, Settling, Active, Toggle, Blocked };

    void clearToggleMemory() noexcept;

    VaaniHotkey m_hotkey;
    bool m_doubleTapEnabled = true;
    State m_state = State::Idle;
    qint64 m_settleSince = 0;

    bool m_settleByPress = true;
    int m_tapCount = 0;
    qint64 m_tapReleasedAt = 0;
    qint64 m_noStartUntil = 0;
    bool m_arming = false;
    qint64 m_activeSince = 0;
    qint64 m_toggleSince = 0;
    bool m_stopArmed = false;
    bool m_stopPending = false;
    qint64 m_stopPressAt = 0;
};