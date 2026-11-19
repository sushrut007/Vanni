#pragma once

#include <QPoint>
#include <QWidget>

class QLabel;
class QPushButton;
class QTextEdit;
class QTimer;

// Modern floating dialog shown when recognized text cannot be pasted into a focused field.
class VaaniDictationCapturePanel : public QWidget
{
    Q_OBJECT

public:
    explicit VaaniDictationCapturePanel(QWidget* parent = nullptr);
    ~VaaniDictationCapturePanel() override;

    void showPhrase(const QString& text, const QString& reason = QString());

protected:
    void showEvent(QShowEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;

private slots:
    void copyToClipboard();
    void dismiss();
    void resetCopyButton();

private:
    void reposition();
    void updateHeightForContent();

    QWidget* m_card = nullptr;
    QLabel* m_badge = nullptr;
    QLabel* m_title = nullptr;
    QLabel* m_hint = nullptr;
    QTextEdit* m_text = nullptr;
    QLabel* m_statusLabel = nullptr;
    QPushButton* m_copyButton = nullptr;
    QPushButton* m_closeButton = nullptr;
    QPushButton* m_headerCloseButton = nullptr;
    QTimer* m_copyFeedbackTimer = nullptr;
    QPoint m_dragPosition;
};