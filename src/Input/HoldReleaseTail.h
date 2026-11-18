#pragma once

#include <QtGlobal>
#include <functional>

// Mic stays open briefly after PTT keys release (Dictation Pro R-3).
class HoldReleaseTail
{
public:
    static constexpr int kTailMs = 200;

    struct Hooks {
        std::function<void(int)> startTimer;
        std::function<void()> stopTimer;
        std::function<void(qint64 msSinceRelease)> stopEngine;
    };

    explicit HoldReleaseTail(Hooks hooks)
        : m_hooks(std::move(hooks))
    {
    }

    void released()
    {
        if (m_hooks.startTimer)
            m_hooks.startTimer(kTailMs);
    }

    void fireStop()
    {
        if (m_hooks.stopEngine)
            m_hooks.stopEngine(kTailMs);
    }

    void flushForNewPress()
    {
        if (m_hooks.stopTimer)
            m_hooks.stopTimer();
        fireStop();
    }

    void cancelling()
    {
        if (m_hooks.stopTimer)
            m_hooks.stopTimer();
    }

private:
    Hooks m_hooks;
};