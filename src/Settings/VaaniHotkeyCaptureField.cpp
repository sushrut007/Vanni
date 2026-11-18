#include "Settings/VaaniHotkeyCaptureField.h"

#include "Input/VaaniHotkey.h"

#include <QFocusEvent>
#include <QKeyEvent>

VaaniHotkeyCaptureField::VaaniHotkeyCaptureField(QWidget* parent)
    : QPushButton(parent)
{
    setObjectName(QStringLiteral("vaaniHotkeyCapture"));
    setCheckable(true);
    setFocusPolicy(Qt::StrongFocus);
    connect(this, &QPushButton::clicked, this, [this]() {
        if (m_capturing)
            cancelCapture();
        else
            startCapture();
    });
    showLabel();
}

VaaniHotkeyCaptureField::~VaaniHotkeyCaptureField()
{
    cancelCapture();
}

void VaaniHotkeyCaptureField::setHotkey(const VaaniHotkey& hotkey)
{
    m_hotkey = hotkey;
    if (!m_capturing)
        showLabel();
}

void VaaniHotkeyCaptureField::startCapture()
{
    if (m_capturing)
        return;
    m_capturing = true;
    m_recording.clear();
    setChecked(true);
    setText(tr("Press your hotkey…"));
    setFocus(Qt::MouseFocusReason);
    emit captureBegan();
}

void VaaniHotkeyCaptureField::cancelCapture()
{
    if (!m_capturing)
        return;
    endCapture();
}

void VaaniHotkeyCaptureField::endCapture()
{
    const bool wasCapturing = m_capturing;
    m_capturing = false;
    m_recording.clear();
    setChecked(false);
    showLabel();
    if (wasCapturing)
        emit captureEnded();
}

bool VaaniHotkeyCaptureField::event(QEvent* event)
{
    if (m_capturing && event->type() == QEvent::ShortcutOverride) {
        event->accept();
        return true;
    }
    return QPushButton::event(event);
}

int VaaniHotkeyCaptureField::nativeVk(const QKeyEvent* event) const
{
    if (!event)
        return 0;
    int vk = event->nativeVirtualKey();
    if (vk <= 0)
        vk = event->key();
    return vk;
}

void VaaniHotkeyCaptureField::keyPressEvent(QKeyEvent* event)
{
    if (!m_capturing) {
        QPushButton::keyPressEvent(event);
        return;
    }
    if (event->key() == Qt::Key_Escape) {
        emit escapePressed();
        cancelCapture();
        event->accept();
        return;
    }

    const int vk = nativeVk(event);
    if (vk > 0 && m_recording.size() < 3 && !m_recording.contains(vk))
        m_recording.push_back(vk);

  VaaniHotkey live;
    live.virtualKeys = m_recording;
    setText(tr("%1 — release to confirm").arg(live.displayName()));
    event->accept();
}

void VaaniHotkeyCaptureField::keyReleaseEvent(QKeyEvent* event)
{
    if (!m_capturing) {
        QPushButton::keyReleaseEvent(event);
        return;
    }
    event->accept();

    if (m_recording.isEmpty())
        return;

    VaaniHotkey probe;
    probe.virtualKeys = m_recording;
    const auto snap = probe.readSnapshot();
    if (snap.fullyHeld || snap.partiallyHeld)
        return;

    const QString err = VaaniReservedKeys::validateHotkey(probe);
    if (!err.isEmpty()) {
        emit hotkeyRefused(err);
        m_recording.clear();
        setText(tr("Press your hotkey…"));
        return;
    }

    m_hotkey = probe;
    emit hotkeyAccepted(probe);
    endCapture();
}

void VaaniHotkeyCaptureField::focusOutEvent(QFocusEvent* event)
{
    if (m_capturing)
        cancelCapture();
    QPushButton::focusOutEvent(event);
}

void VaaniHotkeyCaptureField::showLabel()
{
    setText(m_hotkey.isEmpty() ? tr("Click to set hotkey") : m_hotkey.displayName());
}