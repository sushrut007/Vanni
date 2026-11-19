#include "Input/HoldToTalkTracker.h"

void HoldToTalkTracker::setHotkey(const VaaniHotkey& hotkey) noexcept
{
    m_hotkey = hotkey;
    reset();
}

void HoldToTalkTracker::setDoubleTapToggleEnabled(bool enabled) noexcept
{
    m_doubleTapEnabled = enabled;
}

void HoldToTalkTracker::reset() noexcept
{
    m_state = State::Blocked;
    m_settleSince = 0;
    clearToggleMemory();
    m_noStartUntil = 0;
}

void HoldToTalkTracker::clearToggleMemory() noexcept
{
    m_tapCount = 0;
    m_arming = false;
    m_stopArmed = false;
    m_stopPending = false;
    m_settleByPress = true;
}

HoldToTalkTracker::Step HoldToTalkTracker::feed(bool fullyHeld, bool partiallyHeld, bool otherKeyDown,
                                                qint64 nowMs, bool escapeDown) noexcept
{
    Step step;
    if (m_hotkey.isEmpty()) {
        m_state = State::Idle;
        clearToggleMemory();
        return step;
    }

    const bool anyChord = fullyHeld || partiallyHeld;

    switch (m_state) {
    case State::Idle: {
        if (!anyChord)
            return step;
        if (otherKeyDown) {
            m_state = State::Blocked;
            clearToggleMemory();
            return step;
        }
        if (fullyHeld) {
            if (nowMs < m_noStartUntil) {
                m_state = State::Blocked;
                clearToggleMemory();
                return step;
            }
            if (m_doubleTapEnabled && m_tapCount == 1
                && nowMs - m_tapReleasedAt <= kDoubleTapWindowMs) {
                clearToggleMemory();
                m_state = State::Active;
                m_arming = true;
                m_activeSince = nowMs;
                step.event = Event::Started;
                return step;
            }
            m_state = State::Settling;
            m_settleSince = nowMs;
            m_settleByPress = true;
        }
        return step;
    }
    case State::Settling: {
        if (otherKeyDown) {
            m_state = State::Blocked;
            clearToggleMemory();
            return step;
        }
        if (!fullyHeld) {
            if (partiallyHeld && m_settleByPress && nowMs - m_settleSince < kSettleMs) {
                m_tapCount = 1;
                m_tapReleasedAt = nowMs;
            } else {
                clearToggleMemory();
            }
            m_state = State::Idle;
            return step;
        }
        if (nowMs - m_settleSince >= kSettleMs) {
            clearToggleMemory();
            m_state = State::Active;
            m_arming = false;
            step.event = Event::Started;
        }
        return step;
    }
    case State::Active: {
        if (otherKeyDown) {
            m_state = State::Blocked;
            step.event = Event::Cancelled;
            return step;
        }
        if (fullyHeld) {
            if (m_arming && nowMs - m_activeSince >= kSettleMs)
                m_arming = false;
            return step;
        }
        if (m_arming && nowMs - m_activeSince < kSettleMs) {
            m_arming = false;
            m_state = State::Toggle;
            m_toggleSince = nowMs;
            m_stopArmed = true;
            m_stopPending = false;
            step.event = Event::Toggled;
            return step;
        }
        m_state = State::Blocked;
        m_arming = false;
        step.event = Event::Ended;
        return step;
    }
    case State::Toggle: {
        if (escapeDown) {
            m_state = State::Blocked;
            clearToggleMemory();
            step.event = Event::Cancelled;
            return step;
        }
        if (nowMs - m_toggleSince >= kToggleMaxMs) {
            m_state = State::Blocked;
            clearToggleMemory();
            step.event = Event::Ended;
            return step;
        }
        const bool pressed = !otherKeyDown && fullyHeld;
        if (!anyChord)
            m_stopArmed = true;
        else if (otherKeyDown)
            m_stopArmed = false;
        if (!m_stopPending) {
            if (pressed && m_stopArmed) {
                m_stopPending = true;
                m_stopPressAt = nowMs;
            }
            return step;
        }
        if (otherKeyDown) {
            m_stopPending = false;
            m_stopArmed = false;
            return step;
        }
        if (!fullyHeld) {
            if (!anyChord)
                m_stopPending = false;
            return step;
        }
        if (pressed && nowMs - m_stopPressAt < kSettleMs)
            return step;
        m_state = State::Blocked;
        clearToggleMemory();
        m_noStartUntil = nowMs + kDoubleTapWindowMs;
        step.event = Event::Ended;
        return step;
    }
    case State::Blocked:
        if (!anyChord && !otherKeyDown)
            m_state = State::Idle;
        return step;
    }
    return step;
}