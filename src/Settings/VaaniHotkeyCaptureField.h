#pragma once

#include "Input/VaaniHotkey.h"

#include <QPushButton>
#include <QVector>

class QEvent;

class VaaniHotkeyCaptureField : public QPushButton
{
    Q_OBJECT

public:
    explicit VaaniHotkeyCaptureField(QWidget* parent = nullptr);
    ~VaaniHotkeyCaptureField() override;

    void setHotkey(const VaaniHotkey& hotkey);
    VaaniHotkey hotkey() const noexcept { return m_hotkey; }

    bool isCapturing() const noexcept { return m_capturing; }
    void startCapture();
    void cancelCapture();

signals:
    void captureBegan();
    void captureEnded();
    void escapePressed();
    void hotkeyAccepted(const VaaniHotkey& hotkey);
    void hotkeyRefused(const QString& reason);

protected:
    bool event(QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;

private:
    void endCapture();
    void showLabel();
    int nativeVk(const QKeyEvent* event) const;

    VaaniHotkey m_hotkey;
    QVector<int> m_recording;
    bool m_capturing = false;
};