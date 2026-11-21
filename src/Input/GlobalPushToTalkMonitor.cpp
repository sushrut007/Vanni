#include "Input/GlobalPushToTalkMonitor.h"

#include "Utils/VaaniLogging.h"

#include <QElapsedTimer>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

GlobalPushToTalkMonitor::GlobalPushToTalkMonitor(QObject* parent)
    : QObject(parent)
{
    m_pollTimer.setInterval(kPollIntervalMs);
    m_pollTimer.setTimerType(Qt::CoarseTimer);
    connect(&m_pollTimer, &QTimer::timeout, this, &GlobalPushToTalkMonitor::poll);
    setHotkey(VaaniHotkey::defaultPushToTalk());
}

GlobalPushToTalkMonitor::~GlobalPushToTalkMonitor() = default;

void GlobalPushToTalkMonitor::setHotkey(const VaaniHotkey& hotkey)
{
    const bool unchanged = m_tracker.hotkey().storageString() == hotkey.storageString();
    if (!(unchanged && m_tracker.sessionActive())) {
        const bool wasActive = m_tracker.sessionActive();
        m_tracker.setHotkey(hotkey);
        if (wasActive)
            emit holdCancelled();
    }

    const bool on = hotkey.isValid();
#ifdef Q_OS_WIN
    if (on && !m_pollTimer.isActive())
        m_pollTimer.start();
    else if (!on)
        m_pollTimer.stop();
#else
    Q_UNUSED(on);
#endif
    qCInfo(lcUtils) << "Vaani PTT:" << hotkey.displayName();
}

void GlobalPushToTalkMonitor::poll()
{
#ifdef Q_OS_WIN
    static QElapsedTimer pollClock;
    if (!pollClock.isValid())
        pollClock.start();
    const VaaniHotkey::Snapshot snap = m_tracker.hotkey().readSnapshot();
    feed(snap, pollClock.elapsed(), []() {
#ifdef Q_OS_WIN
        return (GetAsyncKeyState(0x1B) & 0x8000) != 0;
#else
        return false;
#endif
    });
#endif
}

void GlobalPushToTalkMonitor::setDoubleTapToggleEnabled(bool enabled)
{
    m_tracker.setDoubleTapToggleEnabled(enabled);
}

void GlobalPushToTalkMonitor::setSuspended(bool suspended)
{
    if (m_suspended == suspended)
        return;
    m_suspended = suspended;
    const bool wasActive = m_tracker.sessionActive();
    m_tracker.reset();
    if (wasActive)
        emit holdCancelled();
}

void GlobalPushToTalkMonitor::feed(const VaaniHotkey::Snapshot& snap, qint64 nowMs,
                                   const std::function<bool()>& escapeDown)
{
    if (m_suspended)
        return;

    const bool other = snap.fullyHeld && snap.extraKeyDown;
    const bool escape = m_tracker.toggleActive() && escapeDown && escapeDown();
    const HoldToTalkTracker::Step step =
        m_tracker.feed(snap.fullyHeld, snap.partiallyHeld, other, nowMs, escape);
    switch (step.event) {
    case HoldToTalkTracker::Event::Started:
        emit holdStarted();
        break;
    case HoldToTalkTracker::Event::Ended:
        emit holdEnded();
        break;
    case HoldToTalkTracker::Event::Cancelled:
        emit holdCancelled();
        break;
    case HoldToTalkTracker::Event::Toggled:
        emit holdToggled();
        break;
    case HoldToTalkTracker::Event::None:
        break;
    }
}