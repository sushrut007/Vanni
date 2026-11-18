#pragma once

#include "Input/HoldToTalkTracker.h"
#include "Input/VaaniHotkey.h"

#include <QObject>
#include <QTimer>
#include <QtGlobal>
#include <functional>

class GlobalPushToTalkMonitor : public QObject
{
    Q_OBJECT

public:
    explicit GlobalPushToTalkMonitor(QObject* parent = nullptr);
    ~GlobalPushToTalkMonitor() override;

    void setHotkey(const VaaniHotkey& hotkey);
    VaaniHotkey hotkey() const { return m_tracker.hotkey(); }

    void feed(const VaaniHotkey::Snapshot& snap, qint64 nowMs,
              const std::function<bool()>& escapeDown = {});

    void setDoubleTapToggleEnabled(bool enabled);
    void setSuspended(bool suspended);
    bool isSuspended() const noexcept { return m_suspended; }

    bool hotkeyAssigned() const noexcept { return !m_tracker.hotkey().isEmpty(); }

    static constexpr int kPollIntervalMs = 35;

    HoldToTalkTracker& tracker() noexcept { return m_tracker; }

signals:
    void holdStarted();
    void holdEnded();
    void holdCancelled();
    void holdToggled();

private slots:
    void poll();

private:
    HoldToTalkTracker m_tracker;
    QTimer m_pollTimer;
    bool m_suspended = false;
};