#pragma once

#include <QWidget>

class QLabel;
class QPushButton;
class QTextEdit;

// Floating panel when recognized text cannot be pasted into the focused field.
class VaaniDictationCapturePanel : public QWidget
{
    Q_OBJECT

public:
    explicit VaaniDictationCapturePanel(QWidget* parent = nullptr);
    ~VaaniDictationCapturePanel() override;

    void showPhrase(const QString& text);

protected:
    void showEvent(QShowEvent* event) override;

private slots:
    void copyToClipboard();
    void dismiss();

private:
    void reposition();

    QTextEdit* m_text = nullptr;
    QLabel* m_hint = nullptr;
    QPushButton* m_copyButton = nullptr;
    QPushButton* m_closeButton = nullptr;
};